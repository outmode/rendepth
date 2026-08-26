#include "DvdReader.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>

#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#endif

DvdReader::DvdReader() = default;

DvdReader::~DvdReader() {
    close();
}

bool DvdReader::isDvdSource(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty()) return false;

    if (std::filesystem::is_directory(path, ec)) {
        if (std::filesystem::exists(path / "VIDEO_TS" / "VIDEO_TS.IFO", ec) ||
            std::filesystem::exists(path / "video_ts" / "video_ts.ifo", ec) ||
            std::filesystem::exists(path / "VIDEO_TS" / "video_ts.ifo", ec) ||
            std::filesystem::exists(path / "VIDEO_TS", ec) ||
            std::filesystem::exists(path / "video_ts", ec)) {
            return true;
        }
        auto dirName = path.filename().string();
        std::transform(dirName.begin(), dirName.end(), dirName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (dirName == "video_ts") return true;
    } else if (std::filesystem::is_regular_file(path, ec)) {
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".iso") return true;

        auto filename = path.filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (filename == "video_ts.ifo" || filename == "vts_01_0.ifo" ||
            (ext == ".ifo" && filename.rfind("vts_", 0) == 0)) {
            return true;
        }
    }

    std::string pathStr = path.string();
    if (pathStr.rfind("/dev/sr", 0) == 0 ||
        pathStr.rfind("/dev/cdrom", 0) == 0 ||
        pathStr.rfind("/dev/dvd", 0) == 0) {
        return true;
    }

#ifdef _WIN32
    if (pathStr.size() >= 2 && std::isalpha(static_cast<unsigned char>(pathStr[0])) && pathStr[1] == ':') {
        if (pathStr.size() == 2 || (pathStr.size() == 3 && (pathStr[2] == '\\' || pathStr[2] == '/'))) {
            return true;
        }
    }
    if (pathStr.rfind("\\\\.\\", 0) == 0) {
        return true;
    }
#endif

    return false;
}

std::filesystem::path DvdReader::resolveDiscRoot(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        auto filename = path.filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (ext == ".ifo" || ext == ".vob") {
            auto parent = path.parent_path();
            auto parentName = parent.filename().string();
            std::transform(parentName.begin(), parentName.end(), parentName.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (parentName == "video_ts") {
                return parent.parent_path();
            }
            return parent;
        }
    } else if (std::filesystem::is_directory(path, ec)) {
        auto dirName = path.filename().string();
        std::transform(dirName.begin(), dirName.end(), dirName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (dirName == "video_ts") {
            return path.parent_path();
        }
    }
    return path;
}

#ifdef RENDEPTH_ENABLE_DVD

static double parseDvdTime(const dvd_time_t& time) {
    int hours = ((time.hour >> 4) * 10) + (time.hour & 0x0F);
    int minutes = ((time.minute >> 4) * 10) + (time.minute & 0x0F);
    int seconds = ((time.second >> 4) * 10) + (time.second & 0x0F);
    int frames = ((time.frame_u & 0x3F) >> 4) * 10 + (time.frame_u & 0x0F);
    int fpsCode = (time.frame_u >> 6) & 0x03;
    double fps = (fpsCode == 3) ? 29.97 : (fpsCode == 1 ? 25.0 : 29.97);
    return hours * 3600.0 + minutes * 60.0 + seconds + (frames / fps);
}

void DvdReader::inspectTitles() {
    titles_.clear();
    if (!dvd_) return;

    if (!vmgIfo_) {
        vmgIfo_ = ifoOpenVMGI(dvd_);
        if (!vmgIfo_) {
            vmgIfo_ = ifoOpen(dvd_, 0);
        }
    }
    if (!vmgIfo_) return;

    ifoRead_TT_SRPT(vmgIfo_);
    tt_srpt_t* ttSrpt = vmgIfo_->tt_srpt;
    if (!ttSrpt || ttSrpt->nr_of_srpts == 0) return;

    for (uint16_t i = 0; i < ttSrpt->nr_of_srpts; ++i) {
        title_info_t* tinfo = &ttSrpt->title[i];
        int titleSetNr = tinfo->title_set_nr;
        int vtsTtn = tinfo->vts_ttn;

        ifo_handle_t* vtsi = ifoOpenVTSI(dvd_, titleSetNr);
        if (!vtsi) continue;

        ifoRead_VTS_PTT_SRPT(vtsi);
        ifoRead_PGCIT(vtsi);

        DvdTitle title;
        title.titleNum = static_cast<int>(i + 1);
        title.titleSetNum = titleSetNr;
        title.vtsTitleNum = vtsTtn;
        title.angleCount = static_cast<int>(tinfo->nr_of_angles);

        if (vtsi->vts_ptt_srpt && vtsTtn > 0 && vtsTtn <= vtsi->vts_ptt_srpt->nr_of_srpts) {
            ttu_t* ttu = &vtsi->vts_ptt_srpt->title[vtsTtn - 1];
            title.chapterCount = ttu->nr_of_ptts;

            if (ttu->nr_of_ptts > 0 && vtsi->vts_pgcit) {
                ptt_info_t* firstPtt = &ttu->ptt[0];
                int pgcn = firstPtt->pgcn;

                if (pgcn > 0 && pgcn <= vtsi->vts_pgcit->nr_of_pgci_srp) {
                    pgc_t* pgc = vtsi->vts_pgcit->pgci_srp[pgcn - 1].pgc;
                    if (pgc) {
                        title.duration = parseDvdTime(pgc->playback_time);

                        double accumulatedTime = 0.0;
                        for (uint16_t c = 0; c < ttu->nr_of_ptts; ++c) {
                            ptt_info_t* ptt = &ttu->ptt[c];
                            DvdChapter chapter;
                            chapter.index = static_cast<int>(c);
                            chapter.name = "Chapter " + std::to_string(c + 1);

                            int pgn = ptt->pgn;
                            int targetCellIdx = 0;
                            if (pgc->program_map && pgn > 0 && pgn <= pgc->nr_of_programs) {
                                targetCellIdx = pgc->program_map[pgn - 1] - 1;
                            }

                            if (targetCellIdx >= 0 && targetCellIdx < pgc->nr_of_cells && pgc->cell_playback) {
                                chapter.startCell = targetCellIdx;
                                chapter.startSector = pgc->cell_playback[targetCellIdx].first_sector;
                            }

                            if (c == 0) {
                                chapter.startTime = 0.0;
                            } else {
                                chapter.startTime = accumulatedTime;
                            }

                            if (targetCellIdx >= 0 && targetCellIdx < pgc->nr_of_cells && pgc->cell_playback) {
                                chapter.duration = parseDvdTime(pgc->cell_playback[targetCellIdx].playback_time);
                                accumulatedTime += chapter.duration;
                            }

                            title.chapters.push_back(chapter);
                        }
                    }
                }
            }
        }

        titles_.push_back(title);
        ifoClose(vtsi);
    }
}

bool DvdReader::open(const std::filesystem::path& path, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    close();

    auto rootPath = resolveDiscRoot(path);
    discPath_ = rootPath;

    dvd_ = DVDOpen(rootPath.string().c_str());
    if (!dvd_) {
        error = "Failed to open DVD source at " + rootPath.string();
        return false;
    }

    char volid[128] = {0};
    if (DVDUDFVolumeInfo(dvd_, volid, sizeof(volid), nullptr, 0) == 0 && volid[0] != '\0') {
        discTitle_ = volid;
    } else if (DVDISOVolumeInfo(dvd_, volid, sizeof(volid), nullptr, 0) == 0 && volid[0] != '\0') {
        discTitle_ = volid;
    }
    if (discTitle_.empty()) {
        discTitle_ = rootPath.stem().string();
    }

    inspectTitles();

    if (titles_.empty()) {
        error = "No playable titles found on DVD disc.";
        close();
        return false;
    }

    // Auto-select longest title (main feature)
    int bestTitleIndex = 0;
    double maxDuration = 0.0;
    int maxChapters = 0;

    for (size_t i = 0; i < titles_.size(); ++i) {
        const auto& title = titles_[i];
        if (title.duration > maxDuration ||
            (std::abs(title.duration - maxDuration) < 1.0 && title.chapterCount > maxChapters)) {
            maxDuration = title.duration;
            maxChapters = title.chapterCount;
            bestTitleIndex = static_cast<int>(i);
        }
    }

    return selectTitle(bestTitleIndex, error);
}

bool DvdReader::selectTitle(int titleIndex, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!dvd_) {
        error = "DVD reader is not open.";
        return false;
    }

    if (titleIndex < 0 || titleIndex >= static_cast<int>(titles_.size())) {
        error = "Invalid DVD title index " + std::to_string(titleIndex);
        return false;
    }

    if (vtsFile_) {
        DVDCloseFile(vtsFile_);
        vtsFile_ = nullptr;
    }
    if (vtsIfo_) {
        ifoClose(vtsIfo_);
        vtsIfo_ = nullptr;
    }

    const auto& selectedTitle = titles_[titleIndex];
    activeTitle_ = titleIndex;
    activeVts_ = selectedTitle.titleSetNum;
    duration_ = selectedTitle.duration;
    chapterCount_ = selectedTitle.chapterCount;
    chapters_ = selectedTitle.chapters;
    currentChapter_ = 0;

    vtsIfo_ = ifoOpenVTSI(dvd_, activeVts_);
    if (vtsIfo_) {
        ifoRead_VTS_PTT_SRPT(vtsIfo_);
        ifoRead_PGCIT(vtsIfo_);
    }

    vtsFile_ = DVDOpenFile(dvd_, activeVts_, DVD_READ_TITLE_VOBS);
    if (!vtsFile_) {
        error = "Failed to open DVD title set VOBs for VTS " + std::to_string(activeVts_);
        return false;
    }

    ssize_t blocks = DVDFileSize(vtsFile_);
    totalSizeBytes_ = blocks > 0 ? static_cast<int64_t>(blocks) * DVD_VIDEO_LB_LEN : 0;
    currentByteOffset_ = 0;

    return true;
}

void DvdReader::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (vtsFile_) {
        DVDCloseFile(vtsFile_);
        vtsFile_ = nullptr;
    }
    if (vtsIfo_) {
        ifoClose(vtsIfo_);
        vtsIfo_ = nullptr;
    }
    if (vmgIfo_) {
        ifoClose(vmgIfo_);
        vmgIfo_ = nullptr;
    }
    if (dvd_) {
        DVDClose(dvd_);
        dvd_ = nullptr;
    }
    activeTitle_ = -1;
    activeVts_ = -1;
    duration_ = 0.0;
    chapterCount_ = 0;
    currentChapter_ = 0;
    currentByteOffset_ = 0;
    totalSizeBytes_ = 0;
    discTitle_.clear();
    discPath_.clear();
    titles_.clear();
    chapters_.clear();
}

int DvdReader::read(uint8_t* buffer, int size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_ || size <= 0) return 0;
    ssize_t bytesRead = DVDReadBytes(vtsFile_, buffer, static_cast<size_t>(size));
    if (bytesRead > 0) {
        currentByteOffset_ += bytesRead;
    }
    return static_cast<int>(bytesRead);
}

int64_t DvdReader::seek(int64_t offset, int whence) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_) return -1;

    // Handle AVSEEK_SIZE
    if (whence == 0x10000) {
        return totalSizeBytes_ > 0 ? totalSizeBytes_ : -1;
    }

    int seekMode = whence & ~0x20000; // Mask out AVSEEK_FORCE
    int64_t target = 0;
    if (seekMode == 0) { // SEEK_SET
        target = offset;
    } else if (seekMode == 1) { // SEEK_CUR
        target = currentByteOffset_ + offset;
    } else if (seekMode == 2) { // SEEK_END
        if (totalSizeBytes_ <= 0) return -1;
        target = totalSizeBytes_ + offset;
    } else {
        return -1;
    }

    if (target < 0) target = 0;
    if (totalSizeBytes_ > 0 && target > totalSizeBytes_) target = totalSizeBytes_;

    int blockOffset = static_cast<int>(target / DVD_VIDEO_LB_LEN);
    int blockByteOffset = static_cast<int>(target % DVD_VIDEO_LB_LEN);

    if (DVDFileSeek(vtsFile_, blockOffset) < 0) return -1;
    currentByteOffset_ = static_cast<int64_t>(blockOffset) * DVD_VIDEO_LB_LEN;

    if (blockByteOffset > 0) {
        uint8_t dummy[DVD_VIDEO_LB_LEN];
        ssize_t skip = DVDReadBytes(vtsFile_, dummy, blockByteOffset);
        if (skip > 0) {
            currentByteOffset_ += skip;
        }
    }

    return currentByteOffset_;
}

bool DvdReader::seekTime(double seconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_ || duration_ <= 0.0 || totalSizeBytes_ <= 0) return false;

    double ratio = std::clamp(seconds / duration_, 0.0, 1.0);
    int64_t targetByte = static_cast<int64_t>(ratio * totalSizeBytes_);
    return seek(targetByte, 0) >= 0;
}

bool DvdReader::seekChapter(int chapterIndex) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_ || chapterIndex < 0 || chapterIndex >= static_cast<int>(chapters_.size())) return false;

    uint32_t startSector = chapters_[chapterIndex].startSector;
    int64_t targetByte = static_cast<int64_t>(startSector) * DVD_VIDEO_LB_LEN;
    if (seek(targetByte, 0) >= 0) {
        currentChapter_ = chapterIndex;
        return true;
    }
    return false;
}

int DvdReader::chapterCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return chapterCount_;
}

int DvdReader::currentChapter() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (chapters_.empty()) return 0;
    double currentSec = currentTime();
    for (int i = static_cast<int>(chapters_.size()) - 1; i >= 0; --i) {
        if (currentSec >= chapters_[i].startTime) {
            return i;
        }
    }
    return currentChapter_;
}

double DvdReader::chapterStartTime(int chapterIndex) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (chapterIndex >= 0 && chapterIndex < static_cast<int>(chapters_.size())) {
        return chapters_[chapterIndex].startTime;
    }
    return 0.0;
}

const std::vector<DvdChapter>& DvdReader::chapters() const {
    return chapters_;
}

int DvdReader::titleCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(titles_.size());
}

int DvdReader::activeTitle() const {
    return activeTitle_;
}

const std::vector<DvdTitle>& DvdReader::titles() const {
    return titles_;
}

double DvdReader::duration() const {
    return duration_;
}

double DvdReader::currentTime() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (totalSizeBytes_ <= 0 || duration_ <= 0.0) return 0.0;
    double ratio = static_cast<double>(currentByteOffset_) / static_cast<double>(totalSizeBytes_);
    return std::clamp(ratio * duration_, 0.0, duration_);
}

std::string DvdReader::discTitle() const {
    return discTitle_;
}

bool DvdReader::isOpen() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return dvd_ != nullptr && vtsFile_ != nullptr;
}

bool DvdReader::isEncrypted() const {
    return false;
}

#else

bool DvdReader::open(const std::filesystem::path&, std::string& error) {
    error = "DVD playback is not enabled in this build.";
    return false;
}

bool DvdReader::selectTitle(int, std::string& error) {
    error = "DVD playback is not enabled in this build.";
    return false;
}

void DvdReader::close() {}

int DvdReader::read(uint8_t*, int) { return -1; }

int64_t DvdReader::seek(int64_t, int) { return -1; }

bool DvdReader::seekTime(double) { return false; }

bool DvdReader::seekChapter(int) { return false; }

int DvdReader::chapterCount() const { return 0; }

int DvdReader::currentChapter() const { return 0; }

double DvdReader::chapterStartTime(int) const { return 0.0; }

static const std::vector<DvdChapter> emptyDvdChapters;
const std::vector<DvdChapter>& DvdReader::chapters() const { return emptyDvdChapters; }

int DvdReader::titleCount() const { return 0; }

int DvdReader::activeTitle() const { return -1; }

static const std::vector<DvdTitle> emptyDvdTitles;
const std::vector<DvdTitle>& DvdReader::titles() const { return emptyDvdTitles; }

double DvdReader::duration() const { return 0.0; }

double DvdReader::currentTime() const { return 0.0; }

std::string DvdReader::discTitle() const { return ""; }

bool DvdReader::isOpen() const { return false; }

bool DvdReader::isEncrypted() const { return false; }

#endif

#ifdef RENDEPTH_ENABLE_FFMPEG

int DvdReader::readPacket(void* opaque, uint8_t* buf, int buf_size) {
    auto* reader = static_cast<DvdReader*>(opaque);
    if (!reader) return AVERROR_EOF;
    int bytesRead = reader->read(buf, buf_size);
    if (bytesRead == 0) return AVERROR_EOF;
    if (bytesRead < 0) return AVERROR(EIO);
    return bytesRead;
}

int64_t DvdReader::seekPacket(void* opaque, int64_t offset, int whence) {
    auto* reader = static_cast<DvdReader*>(opaque);
    if (!reader) return AVERROR(EIO);
    int64_t result = reader->seek(offset, whence);
    if (result < 0) return AVERROR(EIO);
    return result;
}

AVIOContext* DvdReader::createAVIOContext(int bufferSize) {
    if (!isOpen()) return nullptr;
    auto* buffer = static_cast<unsigned char*>(av_malloc(static_cast<size_t>(bufferSize)));
    if (!buffer) return nullptr;

    AVIOContext* avio = avio_alloc_context(
        buffer,
        bufferSize,
        0, // write_flag = 0 (read-only)
        this,
        &DvdReader::readPacket,
        nullptr,
        &DvdReader::seekPacket
    );

    if (!avio) {
        av_free(buffer);
        return nullptr;
    }

    return avio;
}

#endif
