#include "AudioCdReader.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <limits>
#ifdef __linux__
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <unistd.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#endif
#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#endif

// Expose the selected audio tracks as one virtual WAV stream with chapter offsets at track boundaries.
bool AudioCdStream::open(const std::vector<Track>& tracks, ReadSector readSector) {
    *this = AudioCdStream{};
    if (tracks.empty() || !readSector) return false;
    int previousEnd = 0;
    for (const auto& track : tracks) {
        if (track.firstSector < previousEnd || track.endSector <= track.firstSector) return false;
        chapters_.push_back(double(dataSize_) / 176400.0);
        dataSize_ += int64_t(track.endSector - track.firstSector) * 2352;
        previousEnd = track.endSector;
    }
    if (dataSize_ > UINT32_MAX - 36) return false;
    tracks_ = tracks;
    readSector_ = std::move(readSector);
    auto put = [&](int offset, uint32_t value, int count) {
        for (int i = 0; i < count; ++i) header_[offset + i] = uint8_t(value >> (8 * i));
    };
    std::memcpy(header_.data(), "RIFF", 4);
    put(4, uint32_t(dataSize_) + 36, 4);
    std::memcpy(header_.data() + 8, "WAVEfmt ", 8);
    put(16, 16, 4); put(20, 1, 2); put(22, 2, 2);
    put(24, 44100, 4); put(28, 176400, 4); put(32, 4, 2); put(34, 16, 2);
    std::memcpy(header_.data() + 36, "data", 4);
    put(40, uint32_t(dataSize_), 4);
    return true;
}

// Serve bytes from the WAV header or mapped audio sectors, skipping gaps between tracks.
int AudioCdStream::read(uint8_t* bytes, int size) {
    int copied = 0;
    while (copied < size && position_ < dataSize_ + 44) {
        if (position_ < 44) {
            const int count = std::min<int64_t>(size - copied, 44 - position_);
            std::memcpy(bytes + copied, header_.data() + position_, count);
            position_ += count; copied += count;
            continue;
        }
        int64_t logicalSector = (position_ - 44) / 2352;
        int physicalSector = -1;
        for (const auto& track : tracks_) {
            const int length = track.endSector - track.firstSector;
            if (logicalSector < length) { physicalSector = track.firstSector + int(logicalSector); break; }
            logicalSector -= length;
        }
        if (physicalSector < 0) return copied ? copied : -1;
        if (cachedSector_ != physicalSector) {
            if (!readSector_(physicalSector, sector_.data())) return copied ? copied : -1;
            cachedSector_ = physicalSector;
        }
        const int offset = int((position_ - 44) % 2352);
        const int count = std::min(size - copied, 2352 - offset);
        std::memcpy(bytes + copied, sector_.data() + offset, count);
        position_ += count; copied += count;
    }
    return copied;
}
// Seek within the virtual WAV stream or report its size for FFmpeg.
int64_t AudioCdStream::seek(int64_t offset, int whence) {
    // FFmpeg's AVSEEK_SIZE and AVSEEK_FORCE; kept usable in non-FFmpeg tests.
    if (whence == 0x10000) return dataSize_ + 44;
    whence &= ~0x20000;
    int64_t base = 0;
    if (whence == SEEK_CUR) base = position_;
    else if (whence == SEEK_END) base = dataSize_ + 44;
    else if (whence != SEEK_SET) return -1;
    if (offset < -base || offset > dataSize_ + 44 - base) return -1;
    return position_ = base + offset;
}
// Return the combined audio-track duration in seconds.
double AudioCdStream::duration() const { return double(dataSize_) / 176400.0; }
// Wrap the virtual WAV stream in FFmpeg read and seek callbacks.
AVIOContext* AudioCdStream::createAVIOContext() {
#ifdef RENDEPTH_ENABLE_FFMPEG
    auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
    if (!buffer) return nullptr;
    auto* io = avio_alloc_context(buffer, 32768, 0, this,
        [](void* p, uint8_t* b, int n) {
            int count = static_cast<AudioCdStream*>(p)->read(b, n);
            return count > 0 ? count : (count == 0 ? AVERROR_EOF : AVERROR(EIO));
        }, nullptr, [](void* p, int64_t offset, int whence) -> int64_t {
            return static_cast<AudioCdStream*>(p)->seek(offset, whence);
        });
    if (!io) av_free(buffer);
    return io;
#else
    return nullptr;
#endif
}

struct AudioCdReader::Device {
#ifdef __linux__
    int fd = -1;
    // Close the optical-drive descriptor when its device wrapper is released.
    ~Device() { if (fd >= 0) ::close(fd); }
#elif defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    // Close the Windows optical-drive handle when its device wrapper is released.
    ~Device() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#endif
    std::vector<AudioCdStream::Track> tracks;
    // Open the optical drive and collect playable audio tracks from its table of contents.
    bool open(const std::filesystem::path& path) {
#ifdef __linux__
        fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        cdrom_tochdr header{};
        if (ioctl(fd, CDROMREADTOCHDR, &header) < 0 || header.cdth_trk0 < 1 ||
            header.cdth_trk1 < header.cdth_trk0 || header.cdth_trk1 > 99) return false;
        std::vector<cdrom_tocentry> entries;
        for (int i = header.cdth_trk0; i <= header.cdth_trk1 + 1; ++i) {
            cdrom_tocentry entry{};
            entry.cdte_track = i > header.cdth_trk1 ? CDROM_LEADOUT : i;
            entry.cdte_format = CDROM_LBA;
            if (ioctl(fd, CDROMREADTOCENTRY, &entry) < 0) return false;
            entries.push_back(entry);
        }
        for (size_t i = 0; i + 1 < entries.size(); ++i)
            if (!(entries[i].cdte_ctrl & CDROM_DATA_TRACK))
                tracks.push_back({entries[i].cdte_track, entries[i].cdte_addr.lba, entries[i + 1].cdte_addr.lba});
#elif defined(_WIN32)
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        CDROM_TOC toc{};
        DWORD bytes = 0;
        if (!DeviceIoControl(handle, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &bytes, nullptr) ||
            toc.FirstTrack < 1 || toc.LastTrack < toc.FirstTrack || toc.LastTrack > 99) return false;
        auto lba = [](const TRACK_DATA& t) { return (t.Address[1] * 60 + t.Address[2]) * 75 + t.Address[3] - 150; };
        for (int i = 0; i <= toc.LastTrack - toc.FirstTrack; ++i)
            if (!(toc.TrackData[i].Control & 4))
                tracks.push_back({toc.TrackData[i].TrackNumber, lba(toc.TrackData[i]), lba(toc.TrackData[i + 1])});
#else
        (void)path;
#endif
        return !tracks.empty();
    }
    std::array<uint8_t, 16 * 2352> readAhead{};
    int readAheadStart = -1, readAheadCount = 0;
    // Read an audio sector through a small read-ahead cache bounded by the current track.
    bool read(int sector, uint8_t* bytes) {
        if (sector >= readAheadStart && sector < readAheadStart + readAheadCount) {
            std::memcpy(bytes, readAhead.data() + (sector - readAheadStart) * 2352, 2352);
            return true;
        }
        int count = 0;
        for (const auto& track : tracks)
            if (sector >= track.firstSector && sector < track.endSector)
                count = std::min(16, track.endSector - sector);
        if (!count) return false;
        bool success = false;
#ifdef __linux__
        cdrom_read_audio request{};
        request.addr.lba = sector;
        request.addr_format = CDROM_LBA;
        request.nframes = count;
        request.buf = readAhead.data();
        for (int attempt = 0; attempt < 3; ++attempt)
            if (ioctl(fd, CDROMREADAUDIO, &request) == 0) { success = true; break; }
#elif defined(_WIN32)
        RAW_READ_INFO request{};
        request.DiskOffset.QuadPart = int64_t(sector) * 2048;
        request.SectorCount = count;
        request.TrackMode = CDDA;
        DWORD received = 0;
        for (int attempt = 0; attempt < 3; ++attempt)
            if (DeviceIoControl(handle, IOCTL_CDROM_RAW_READ, &request, sizeof(request), readAhead.data(), count * 2352, &received, nullptr) && received == DWORD(count * 2352)) { success = true; break; }
#endif
        if (!success) { readAheadCount = 0; return false; }
        readAheadStart = sector; readAheadCount = count;
        std::memcpy(bytes, readAhead.data(), 2352);
        return true;
    }
};
// Construct an unopened audio-CD reader.
AudioCdReader::AudioCdReader() = default;
// Release the owned drive and stream state when the reader is destroyed.
AudioCdReader::~AudioCdReader() = default;
// Resolve a supported drive path or audio-CD URI to its underlying optical device.
std::filesystem::path AudioCdReader::devicePath(const std::filesystem::path& path) {
    const auto value = path.string();
#ifdef __linux__
    if (value.starts_with("/dev/")) {
        std::error_code ec;
        const auto canonical = std::filesystem::canonical(path, ec);
        const auto name = canonical.filename().string();
        if (!ec && name.starts_with("sr") && name.size() > 2 &&
            name.find_first_not_of("0123456789", 2) == std::string::npos &&
            std::filesystem::is_block_file(canonical, ec)) return canonical;
    }
    // GVfs exposes audio discs as cdda://sr0/ and cdda:host=sr0/Track*.wav.
    std::string device;
    if (value.starts_with("cdda://")) device = value.substr(7, value.find('/', 7) - 7);
    const auto gvfs = value.find("/cdda:host=");
    if (gvfs != std::string::npos) {
        const auto start = gvfs + 11;
        device = value.substr(start, value.find('/', start) - start);
    }
    if (device.starts_with("sr") && device.size() > 2 &&
        device.find_first_not_of("0123456789", 2) == std::string::npos) return "/dev/" + device;
#elif defined(_WIN32)
    if (value.size() >= 2 && value[1] == ':') {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        const bool rootSelected = value.size() == 2 ||
            (value.size() == 3 && (value[2] == '\\' || value[2] == '/'));
        const auto root = path.wstring().substr(0, 2) + L"\\";
        if ((rootSelected || extension == ".cda") && GetDriveTypeW(root.c_str()) == DRIVE_CDROM)
            return L"\\\\.\\" + path.wstring().substr(0, 2);
    }
    if (value.size() == 6 && value.starts_with("\\\\.\\") && value[5] == ':') {
        const auto root = path.wstring().substr(4) + L"\\";
        if (GetDriveTypeW(root.c_str()) == DRIVE_CDROM) return path;
    }
#endif
    return {};
}
// Probe whether the selected optical drive contains readable audio tracks.
bool AudioCdReader::isAudioCd(const std::filesystem::path& path) {
    auto device = devicePath(path);
    return !device.empty() && Device{}.open(device);
}
// Open an audio CD and connect its physical sector reader to the virtual WAV stream.
bool AudioCdReader::open(const std::filesystem::path& path, std::string& error) {
    device_ = std::make_unique<Device>();
    auto device = devicePath(path);
    if (device.empty() || !device_->open(device) ||
        !stream_.open(device_->tracks, [this](int sector, uint8_t* bytes) { return device_->read(sector, bytes); })) {
        error = "Could not read audio CD tracks. Check the disc and drive permissions.";
        device_.reset();
        return false;
    }
    return true;
}
