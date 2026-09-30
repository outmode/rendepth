#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AVIOContext;

// A seekable WAV view over audio sectors. Data tracks never enter this stream.
class AudioCdStream {
public:
    struct Track { int number; int firstSector; int endSector; };
    // The backend supplies 2352 bytes of stereo, signed 16-bit little-endian PCM.
    using ReadSector = std::function<bool(int, uint8_t*)>;
    bool open(const std::vector<Track>& tracks, ReadSector readSector);
    int read(uint8_t* bytes, int size);
    int64_t seek(int64_t offset, int whence);
    double duration() const;
    const std::vector<double>& chapters() const { return chapters_; }
    AVIOContext* createAVIOContext();
private:
    std::vector<Track> tracks_;
    std::vector<double> chapters_;
    ReadSector readSector_;
    std::array<uint8_t, 44> header_{};
    std::array<uint8_t, 2352> sector_{};
    int cachedSector_ = -1;
    int64_t position_ = 0, dataSize_ = 0;
};

class AudioCdReader {
public:
    AudioCdReader();
    ~AudioCdReader();
    static std::filesystem::path devicePath(const std::filesystem::path& path);
    static bool isAudioCd(const std::filesystem::path& path);
    bool open(const std::filesystem::path& path, std::string& error);
    AudioCdStream& stream() { return stream_; }
private:
    struct Device;
    std::unique_ptr<Device> device_;
    AudioCdStream stream_;
};
