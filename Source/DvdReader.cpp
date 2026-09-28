#include "DiscSource.h"
#include "DvdReader.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#endif

// Construct a reader with no DVD source open.
DvdReader::DvdReader() = default;

// Close DVD resources when the reader is destroyed.
DvdReader::~DvdReader() {
    close();
}

// Determine whether a path identifies a DVD source.
bool DvdReader::isDvdSource(const std::filesystem::path& path) {
    return DiscSource::detect(path) == DiscSource::Type::Dvd;
}

// Normalize a DVD folder or metadata-file selection to its disc root.
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

// Convert the DVD's packed decimal time and frame-rate fields into seconds.
static double parseDvdTime(const dvd_time_t& time) {
    int hours = ((time.hour >> 4) * 10) + (time.hour & 0x0F);
    int minutes = ((time.minute >> 4) * 10) + (time.minute & 0x0F);
    int seconds = ((time.second >> 4) * 10) + (time.second & 0x0F);
    int frames = ((time.frame_u & 0x3F) >> 4) * 10 + (time.frame_u & 0x0F);
    int fpsCode = (time.frame_u >> 6) & 0x03;
    double fps = (fpsCode == 3) ? 29.97 : (fpsCode == 1 ? 25.0 : 29.97);
    return hours * 3600.0 + minutes * 60.0 + seconds + (frames / fps);
}

// Inspect DVD title and program-chain metadata to build durations and chapter locations.
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

                        if (vtsi->vtsi_mat) {
                            for (int audio = 0; audio < std::min<int>(8, vtsi->vtsi_mat->nr_of_vts_audio_streams); ++audio) {
                                if (!(pgc->audio_control[audio] & 0x8000)) continue;
                                ++title.audioTrackCount;
                                const auto& attr = vtsi->vtsi_mat->vts_audio_attr[audio];
                                // DVD audio code extensions 3 and 4 identify commentary.
                                title.commentaryAvailable |= attr.code_extension == 3 || attr.code_extension == 4;
                                if (attr.lang_type == 1 && attr.lang_code) {
                                    const std::string language{static_cast<char>(attr.lang_code >> 8), static_cast<char>(attr.lang_code & 0xff)};
                                    if (std::find(title.audioLanguages.begin(), title.audioLanguages.end(), language) == title.audioLanguages.end())
                                        title.audioLanguages.push_back(language);
                                }
                            }
                        }

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

// Open the disc, inspect available titles, and select the best initial playback title.
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

// Open the selected title's VOB data and refresh its duration, chapters, and byte range.
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
        ifoRead_VTS_TMAPT(vtsIfo_);
    }

    vtsFile_ = DVDOpenFile(dvd_, activeVts_, DVD_READ_TITLE_VOBS);
    if (!vtsFile_) {
        error = "Failed to open DVD title set VOBs for VTS " + std::to_string(activeVts_);
        return false;
    }

    ssize_t blocks = DVDFileSize(vtsFile_);
    totalSizeBytes_ = blocks > 0 ? static_cast<int64_t>(blocks) * DVD_VIDEO_LB_LEN : 0;
    currentByteOffset_ = 0;
    seekStartTime_ = 0.0;

    return true;
}

// Close VOB and metadata handles and reset cached disc state.
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
    seekStartTime_ = 0.0;
    discTitle_.clear();
    discPath_.clear();
    titles_.clear();
    chapters_.clear();
}

// Serve arbitrary byte reads from DVD sectors, including unaligned requests and partial reads.
int DvdReader::read(uint8_t* buffer, int size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_ || size <= 0) return 0;
    if (!buffer) return -1;
    int copied = 0;
    while (copied < size && currentByteOffset_ < totalSizeBytes_) {
        const auto sector = currentByteOffset_ / DVD_VIDEO_LB_LEN;
        if (sector > std::numeric_limits<int>::max()) return copied ? copied : -1;
        const int withinSector = static_cast<int>(currentByteOffset_ % DVD_VIDEO_LB_LEN);
        const int remaining = static_cast<int>(std::min<int64_t>(
            size - copied, totalSizeBytes_ - currentByteOffset_));
        int bytesRead;
        // VOB data must use DVDReadBlocks: DVDReadBytes is for IFO files
        // and does not take the CSS decryption path. FFmpeg can request
        // arbitrary byte ranges, so handle partial sectors explicitly.
        if (withinSector != 0 || remaining < DVD_VIDEO_LB_LEN) {
            unsigned char data[DVD_VIDEO_LB_LEN];
            const auto blocks = DVDReadBlocks(vtsFile_, static_cast<int>(sector), 1, data);
            if (blocks <= 0) return copied ? copied : (blocks < 0 ? -1 : 0);
            bytesRead = std::min(remaining, DVD_VIDEO_LB_LEN - withinSector);
            std::memcpy(buffer + copied, data + withinSector, bytesRead);
        } else {
            const auto blocks = DVDReadBlocks(vtsFile_, static_cast<int>(sector),
                static_cast<size_t>(remaining / DVD_VIDEO_LB_LEN), buffer + copied);
            if (blocks <= 0) return copied ? copied : (blocks < 0 ? -1 : 0);
            bytesRead = static_cast<int>(blocks * DVD_VIDEO_LB_LEN);
        }
        currentByteOffset_ += bytesRead;
        copied += bytesRead;
    }
    return copied;
}

// Update the 64-bit byte cursor or answer size queries without losing offsets in large titles.
int64_t DvdReader::seek(int64_t offset, int whence) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_) return -1;

    const int seekMode = whence & ~0x20000; // Mask out AVSEEK_FORCE
    if (seekMode == 0x10000) { // AVSEEK_SIZE
        return totalSizeBytes_ > 0 ? totalSizeBytes_ : -1;
    }

    int64_t base = 0;
    if (seekMode == 1) { // SEEK_CUR
        base = currentByteOffset_;
    } else if (seekMode == 2) { // SEEK_END
        if (totalSizeBytes_ <= 0) return -1;
        base = totalSizeBytes_;
    } else if (seekMode != 0) { // SEEK_SET uses base zero
        return -1;
    }
    if (offset > std::numeric_limits<int64_t>::max() - base) return -1;
    int64_t target = base + offset;

    if (target < 0) target = 0;
    if (totalSizeBytes_ > 0 && target > totalSizeBytes_) target = totalSizeBytes_;

    // DVDReadBlocks takes an explicit sector offset; no DVDFileSeek is
    // needed. Keep the byte cursor in 64 bits for titles larger than 2 GiB.
    currentByteOffset_ = target;

    return currentByteOffset_;
}

// Return the byte cursor within the active title, or an invalid position when closed.
int64_t DvdReader::bytePosition() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return dvd_ ? currentByteOffset_ : -1;
}

// Estimate a seek sector from title timing and DVD navigation metadata.
bool DvdReader::seekTime(double seconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsFile_ || !vtsIfo_ || !vtsIfo_->vts_ptt_srpt || !vtsIfo_->vts_pgcit ||
        activeTitle_ < 0 || !std::isfinite(seconds) || duration_ <= 0.0) return false;
    const int title = titles_[activeTitle_].vtsTitleNum;
    if (title <= 0 || title > vtsIfo_->vts_ptt_srpt->nr_of_srpts) return false;
    const auto& parts = vtsIfo_->vts_ptt_srpt->title[title - 1];
    if (!parts.nr_of_ptts || !parts.ptt) return false;
    const int pgcn = parts.ptt[0].pgcn;
    if (pgcn <= 0 || pgcn > vtsIfo_->vts_pgcit->nr_of_pgci_srp) return false;
    const auto* pgc = vtsIfo_->vts_pgcit->pgci_srp[pgcn - 1].pgc;
    if (!pgc || !pgc->nr_of_cells || !pgc->cell_playback) return false;
    seconds = std::clamp(seconds, 0.0, duration_);
    uint32_t sector = pgc->cell_playback[0].first_sector;
    double accessTime = 0.0;

    // VOB size is not a time index: variable bitrate and unrelated material
    // in the title set can put a proportional byte seek far beyond the target.
    // Time-map entry zero describes tmu seconds, not the start of the title.
    const auto* maps = vtsIfo_->vts_tmapt;
    if (maps && maps->tmap && pgcn <= maps->nr_of_tmaps &&
        maps->tmap[pgcn - 1].tmu && maps->tmap[pgcn - 1].nr_of_entries &&
        maps->tmap[pgcn - 1].map_ent) {
        const auto& map = maps->tmap[pgcn - 1];
        const int entry = std::min(static_cast<int>(seconds / map.tmu) - 1,
            static_cast<int>(map.nr_of_entries) - 1);
        if (entry >= 0) {
            sector = map.map_ent[entry] & 0x7fffffffU;
            accessTime = (entry + 1) * static_cast<double>(map.tmu);
        }
    } else {
        // Start at the containing cell's access point and let the decoder
        // discard preroll. Skip alternate angle cells when counting time.
        double elapsed = 0.0;
        for (int i = 0; i < pgc->nr_of_cells; ++i) {
            const auto& cell = pgc->cell_playback[i];
            if (cell.block_type == BLOCK_TYPE_ANGLE_BLOCK &&
                cell.block_mode != BLOCK_MODE_FIRST_CELL) continue;
            sector = cell.first_sector;
            accessTime = elapsed;
            elapsed += parseDvdTime(cell.playback_time);
            if (elapsed > seconds) break;
        }
    }
    const int64_t targetByte = static_cast<int64_t>(sector) * DVD_VIDEO_LB_LEN;
    if (targetByte >= totalSizeBytes_) return false;
    if (seek(targetByte, 0) != targetByte) return false;
    seekStartTime_ = accessTime;
    return true;
}

// Return the title time represented by the access point chosen for the last time seek.
double DvdReader::seekStartTime() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return seekStartTime_;
}

// Seek to a chapter's starting sector and record the selection.
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

// Return the number of chapters in the selected title.
int DvdReader::chapterCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return chapterCount_;
}

// Locate the chapter containing the estimated current playback time.
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

// Return a chapter start time in seconds, or zero for an invalid index.
double DvdReader::chapterStartTime(int chapterIndex) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (chapterIndex >= 0 && chapterIndex < static_cast<int>(chapters_.size())) {
        return chapters_[chapterIndex].startTime;
    }
    return 0.0;
}

// Expose the cached chapter metadata for the selected title.
const std::vector<DvdChapter>& DvdReader::chapters() const {
    return chapters_;
}

// Return the number of discovered disc titles.
int DvdReader::titleCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(titles_.size());
}

// Return the selected title index.
int DvdReader::activeTitle() const {
    return activeTitle_;
}

// Expose the cached list of available disc titles.
const std::vector<DvdTitle>& DvdReader::titles() const {
    return titles_;
}

// Return the selected title duration in seconds.
double DvdReader::duration() const {
    return duration_;
}

// Read the selected title's intended 4:3 or 16:9 presentation ratio from its IFO.
double DvdReader::displayAspectRatio() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!vtsIfo_ || !vtsIfo_->vtsi_mat) return 0.0;
    return vtsIfo_->vtsi_mat->vts_video_attr.display_aspect_ratio ? 16.0 / 9.0 : 4.0 / 3.0;
}

// Estimate playback time from the byte cursor's fraction of the title data.
double DvdReader::currentTime() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (totalSizeBytes_ <= 0 || duration_ <= 0.0) return 0.0;
    double ratio = static_cast<double>(currentByteOffset_) / static_cast<double>(totalSizeBytes_);
    return std::clamp(ratio * duration_, 0.0, duration_);
}

// Return the disc name retained when the source was opened.
std::string DvdReader::discTitle() const {
    return discTitle_;
}

// Report whether the reader has an active disc source.
bool DvdReader::isOpen() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return dvd_ != nullptr && vtsFile_ != nullptr;
}

// Return the reader's current encryption status; this backend does not detect encryption here.
bool DvdReader::isEncrypted() const {
    return false;
}

#else

// Report that disc playback is unavailable in this build.
bool DvdReader::open(const std::filesystem::path&, std::string& error) {
    error = "DVD playback is not enabled in this build.";
    return false;
}

// Report that disc playback is unavailable in this build.
bool DvdReader::selectTitle(int, std::string& error) {
    error = "DVD playback is not enabled in this build.";
    return false;
}

// Provide the unavailable-backend fallback for cleanup.
void DvdReader::close() {}

// Provide the unavailable-backend fallback for reading.
int DvdReader::read(uint8_t*, int) { return -1; }

// Provide the unavailable-backend fallback for byte seeking.
int64_t DvdReader::seek(int64_t, int) { return -1; }

// Provide the unavailable-backend fallback for the byte cursor.
int64_t DvdReader::bytePosition() const { return -1; }

// Provide the unavailable-backend fallback for time seeking.
bool DvdReader::seekTime(double) { return false; }

double DvdReader::seekStartTime() const { return 0.0; }

// Provide the unavailable-backend fallback for chapter seeking.
bool DvdReader::seekChapter(int) { return false; }

// Provide the unavailable-backend fallback for chapter count.
int DvdReader::chapterCount() const { return 0; }

// Provide the unavailable-backend fallback for the current chapter.
int DvdReader::currentChapter() const { return 0; }

// Provide the unavailable-backend fallback for chapter timing.
double DvdReader::chapterStartTime(int) const { return 0.0; }

static const std::vector<DvdChapter> emptyDvdChapters;
// Provide the unavailable-backend fallback for chapter metadata.
const std::vector<DvdChapter>& DvdReader::chapters() const { return emptyDvdChapters; }

// Provide the unavailable-backend fallback for title count.
int DvdReader::titleCount() const { return 0; }

// Provide the unavailable-backend fallback for title selection.
int DvdReader::activeTitle() const { return -1; }

static const std::vector<DvdTitle> emptyDvdTitles;
// Provide the unavailable-backend fallback for title metadata.
const std::vector<DvdTitle>& DvdReader::titles() const { return emptyDvdTitles; }

// Provide the unavailable-backend fallback for duration.
double DvdReader::duration() const { return 0.0; }

// Provide the unavailable-backend fallback for display metadata.
double DvdReader::displayAspectRatio() const { return 0.0; }

// Provide the unavailable-backend fallback for playback time.
double DvdReader::currentTime() const { return 0.0; }

// Provide the unavailable-backend fallback for the disc name.
std::string DvdReader::discTitle() const { return ""; }

// Provide the unavailable-backend fallback for open state.
bool DvdReader::isOpen() const { return false; }

// Provide the unavailable-backend fallback for encryption detection.
bool DvdReader::isEncrypted() const { return false; }

#endif

#ifdef RENDEPTH_ENABLE_FFMPEG

// Adapt disc reads to FFmpeg callbacks, translating EOF and I/O failures.
int DvdReader::readPacket(void* opaque, uint8_t* buf, int buf_size) {
    auto* reader = static_cast<DvdReader*>(opaque);
    if (!reader) return AVERROR_EOF;
    int bytesRead = reader->read(buf, buf_size);
    if (bytesRead == 0) return AVERROR_EOF;
    if (bytesRead < 0) return AVERROR(EIO);
    return bytesRead;
}

// Adapt disc byte seeks to FFmpeg error and offset conventions.
int64_t DvdReader::seekPacket(void* opaque, int64_t offset, int whence) {
    auto* reader = static_cast<DvdReader*>(opaque);
    if (!reader) return AVERROR(EIO);
    int64_t result = reader->seek(offset, whence);
    if (result < 0) return AVERROR(EIO);
    return result;
}

// Create a seekable FFmpeg input context backed by this disc reader.
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

    avio->seekable |= AVIO_SEEKABLE_NORMAL;

    return avio;
}

#endif
