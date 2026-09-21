#include "DiscSource.h"
#include "BlurayReader.h"
#include "BlurayPlaylist.h"
#include "BlurayTitleNames.h"
#include <cstdlib>
#include <format>
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

BlurayReader::BlurayReader() = default;

BlurayReader::~BlurayReader() {
    close();
}

bool BlurayReader::isBluraySource(const std::filesystem::path& path) {
    return DiscSource::detect(path) == DiscSource::Type::Bluray;
}

std::filesystem::path BlurayReader::resolveDiscRoot(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        auto filename = path.filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (filename == "index.bdmv" || filename == "movieobject.bdmv") {
            auto parent = path.parent_path();
            auto parentName = parent.filename().string();
            std::transform(parentName.begin(), parentName.end(), parentName.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (parentName == "bdmv") {
                return parent.parent_path();
            }
            return parent;
        }
    } else if (std::filesystem::is_directory(path, ec)) {
        auto dirName = path.filename().string();
        std::transform(dirName.begin(), dirName.end(), dirName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (dirName == "bdmv") {
            return path.parent_path();
        }
    }
    return path;
}

#ifdef RENDEPTH_ENABLE_BLURAY

void BlurayReader::inspectTitles() {
    titles_.clear();
    if (!bd_) return;

    uint32_t numTitles = bd_get_titles(bd_, TITLES_RELEVANT, 0);
    if (numTitles == 0) {
        numTitles = bd_get_titles(bd_, TITLES_ALL, 0);
    }

    const int mainTitle = bd_get_main_title(bd_);
    // Disc-index title numbers and playlist indices are different namespaces.
    // Resolve names only through explicit HDMV playlist references.
    std::map<uint32_t, std::set<std::string>> playlistNames;
    void* movieObjects = nullptr;
    int64_t movieObjectsSize = 0;
    if (bd_read_file(bd_, "BDMV/MovieObject.bdmv", &movieObjects, &movieObjectsSize) > 0 &&
        movieObjects && movieObjectsSize > 0) {
        const auto references = BlurayTitleNames::playlistReferences({
            static_cast<const uint8_t*>(movieObjects), static_cast<size_t>(movieObjectsSize)});
        const auto* disc = bd_get_disc_info(bd_);
        if (disc && disc->titles) {
            for (unsigned n = 1; n <= disc->num_titles; ++n) {
                const auto* entry = disc->titles[n];
                if (!entry || entry->bdj || !entry->name || !entry->name[0] ||
                    entry->id_ref >= references.size()) continue;
                for (const auto playlist : references[entry->id_ref])
                    playlistNames[playlist].insert(entry->name);
            }
        }
    }
    std::free(movieObjects);
    std::map<int, std::string> contentKeys;
    for (uint32_t i = 0; i < numTitles; ++i) {
        BLURAY_TITLE_INFO* info = bd_get_title_info(bd_, i, 0);
        if (info) {
            BlurayTitle title;
            title.index = static_cast<int>(i);
            title.playlist = info->playlist;
            if (const auto names = playlistNames.find(title.playlist);
                names != playlistNames.end() && names->second.size() == 1)
                title.name = *names->second.begin();
            title.duration = static_cast<double>(info->duration) / 90000.0;
            title.chapterCount = info->chapter_count;
            title.angleCount = static_cast<int>(info->angle_count);
            title.mainFeatureCandidate = static_cast<int>(i) == mainTitle;
            void* playlistData = nullptr;
            int64_t playlistSize = 0;
            const auto playlistPath = std::format("BDMV/PLAYLIST/{:05}.mpls", info->playlist);
            if (bd_read_file(bd_, playlistPath.c_str(), &playlistData, &playlistSize) > 0 &&
                playlistData && playlistSize > 0) {
                title.mvc = BlurayPlaylist::hasMvc({static_cast<const uint8_t*>(playlistData),
                    static_cast<size_t>(playlistSize)});
            }
            std::free(playlistData);
            for (uint32_t clip = 0; info->clips && clip < info->clip_count; ++clip) {
                title.audioTrackCount = std::max(title.audioTrackCount, static_cast<int>(info->clips[clip].audio_stream_count));
                const auto& streams = info->clips[clip];
                contentKeys[title.index] += std::format("{}:{}:{};", streams.clip_id,
                    streams.in_time, streams.out_time);
                const auto collectLanguages = [](const BLURAY_STREAM_INFO* tracks, unsigned count,
                    std::map<int, std::string>& languages) {
                    for (unsigned track = 0; tracks && track < count; ++track) {
                        const auto* code = reinterpret_cast<const char*>(tracks[track].lang);
                        std::string language(code, std::find(code, code + 3, '\0'));
                        if (!language.empty() && language != "und")
                            languages.try_emplace(tracks[track].pid, language);
                    }
                };
                collectLanguages(streams.audio_streams, streams.audio_stream_count, title.audioStreamLanguages);
                collectLanguages(streams.sec_audio_streams, streams.sec_audio_stream_count, title.audioStreamLanguages);
                collectLanguages(streams.pg_streams, streams.pg_stream_count, title.subtitleStreamLanguages);
                for (unsigned audio = 0; streams.audio_streams && audio < streams.audio_stream_count; ++audio) {
                    const auto* code = reinterpret_cast<const char*>(streams.audio_streams[audio].lang);
                    std::string language(code, std::find(code, code + 3, '\0'));
                    if (!language.empty() && language != "und" &&
                        std::find(title.audioLanguages.begin(), title.audioLanguages.end(), language) == title.audioLanguages.end())
                        title.audioLanguages.push_back(language);
                }
            }

            for (uint32_t c = 0; c < info->chapter_count && info->chapters; ++c) {
                BlurayChapter chapter;
                chapter.index = static_cast<int>(c);
                chapter.startTime = static_cast<double>(info->chapters[c].start) / 90000.0;
                chapter.duration = static_cast<double>(info->chapters[c].duration) / 90000.0;
                chapter.name = "Chapter " + std::to_string(c + 1);
                title.chapters.push_back(chapter);
            }

            titles_.push_back(title);
            bd_free_title_info(info);
        }
    }
    // Alternate playlists can use exactly the same footage with different
    // audio, chapter, or stereo settings. Share a name only if unambiguous.
    std::map<std::string, std::set<std::string>> contentNames;
    for (const auto& title : titles_)
        if (!title.name.empty() && !contentKeys[title.index].empty())
            contentNames[contentKeys[title.index]].insert(title.name);
    for (auto& title : titles_) {
        if (!title.name.empty()) continue;
        const auto names = contentNames.find(contentKeys[title.index]);
        if (names != contentNames.end() && names->second.size() == 1)
            title.name = *names->second.begin();
    }
}

bool BlurayReader::open(const std::filesystem::path& path, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    close();

    auto rootPath = resolveDiscRoot(path);
    discPath_ = rootPath;

    bd_ = bd_open(rootPath.string().c_str(), nullptr);
    if (!bd_) {
        error = "Failed to open Blu-ray source at " + rootPath.string();
        return false;
    }

    const BLURAY_DISC_INFO* discInfo = bd_get_disc_info(bd_);
    if (discInfo) {
        if ((discInfo->aacs_detected && (!discInfo->aacs_handled || !discInfo->libaacs_detected)) ||
            (discInfo->bdplus_detected && (!discInfo->bdplus_handled || !discInfo->libbdplus_detected)) ||
            discInfo->aacs_error_code != 0) {
            error = "Blu-ray disc is encrypted (AACS/BD+ protection) and could not be decrypted. Encrypted Disc Not Supported";
            bd_close(bd_);
            bd_ = nullptr;
            return false;
        }
        if (discInfo->disc_name && discInfo->disc_name[0] != '\0') {
            discTitle_ = reinterpret_cast<const char*>(discInfo->disc_name);
        } else if (discInfo->udf_volume_id && discInfo->udf_volume_id[0] != '\0') {
            discTitle_ = reinterpret_cast<const char*>(discInfo->udf_volume_id);
        }
    }
    if (discTitle_.empty()) {
        discTitle_ = rootPath.stem().string();
    }

    inspectTitles();

    if (titles_.empty()) {
        const BLURAY_DISC_INFO* currentDiscInfo = bd_get_disc_info(bd_);
        if (currentDiscInfo && (currentDiscInfo->aacs_detected || currentDiscInfo->bdplus_detected)) {
            error = "Blu-ray disc is encrypted and could not be decrypted. Encrypted Disc Not Supported";
        } else {
            error = "No playable titles found on Blu-ray disc.";
        }
        bd_close(bd_);
        bd_ = nullptr;
        return false;
    }

    // Identify main feature: title with the longest duration
    int bestTitleIndex = 0;
    double maxDuration = 0.0;
    int maxChapters = 0;

    for (const auto& title : titles_) {
        if (title.duration > maxDuration ||
            (std::abs(title.duration - maxDuration) < 1.0 && title.chapterCount > maxChapters)) {
            maxDuration = title.duration;
            maxChapters = title.chapterCount;
            bestTitleIndex = title.index;
        }
    }

    if (!selectTitle(bestTitleIndex, error)) {
        close();
        return false;
    }

    const BLURAY_DISC_INFO* postDiscInfo = bd_get_disc_info(bd_);
    if (postDiscInfo) {
        if ((postDiscInfo->aacs_detected && (!postDiscInfo->aacs_handled || !postDiscInfo->libaacs_detected)) ||
            (postDiscInfo->bdplus_detected && (!postDiscInfo->bdplus_handled || !postDiscInfo->libbdplus_detected)) ||
            postDiscInfo->aacs_error_code != 0) {
            error = "Blu-ray disc is encrypted (AACS/BD+ protection) and could not be decrypted. Encrypted Disc Not Supported";
            bd_close(bd_);
            bd_ = nullptr;
            return false;
        }
    }

    return true;
}

bool BlurayReader::selectTitle(int titleIndex, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) {
        error = "Blu-ray reader is not open.";
        return false;
    }

    if (bd_select_title(bd_, static_cast<uint32_t>(titleIndex)) <= 0) {
        error = "Failed to select Blu-ray title " + std::to_string(titleIndex);
        return false;
    }

    activeTitle_ = titleIndex;
    chapters_.clear();
    duration_ = 0.0;
    chapterCount_ = 0;

    for (const auto& title : titles_) {
        if (title.index == titleIndex) {
            duration_ = title.duration;
            chapterCount_ = title.chapterCount;
            chapters_ = title.chapters;
            break;
        }
    }

    return true;
}

void BlurayReader::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (bd_) {
        bd_close(bd_);
        bd_ = nullptr;
    }
    activeTitle_ = -1;
    duration_ = 0.0;
    chapterCount_ = 0;
    discTitle_.clear();
    discPath_.clear();
    titles_.clear();
    chapters_.clear();
}

int BlurayReader::read(uint8_t* buffer, int size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return -1;
    int bytesRead = bd_read(bd_, buffer, size);
    if (bytesRead == 0) return 0; // EOF
    if (bytesRead < 0) return -1;
    return bytesRead;
}

int64_t BlurayReader::seek(int64_t offset, int whence) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return -1;

    // Handle AVSEEK_SIZE
    if (whence == 0x10000) {
        uint64_t titleSize = bd_get_title_size(bd_);
        return titleSize > 0 ? static_cast<int64_t>(titleSize) : -1;
    }

    int seekMode = whence & ~0x20000; // Mask out AVSEEK_FORCE if set
    if (seekMode == 0) { // SEEK_SET
        return bd_seek(bd_, static_cast<uint64_t>(offset >= 0 ? offset : 0));
    } else if (seekMode == 1) { // SEEK_CUR
        int64_t current = bd_tell(bd_);
        if (current < 0) return -1;
        int64_t target = current + offset;
        return bd_seek(bd_, static_cast<uint64_t>(target >= 0 ? target : 0));
    } else if (seekMode == 2) { // SEEK_END
        uint64_t titleSize = bd_get_title_size(bd_);
        if (titleSize == 0) return -1;
        int64_t target = static_cast<int64_t>(titleSize) + offset;
        return bd_seek(bd_, static_cast<uint64_t>(target >= 0 ? target : 0));
    }
    return -1;
}

int64_t BlurayReader::bytePosition() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bd_ ? static_cast<int64_t>(bd_tell(bd_)) : -1;
}

bool BlurayReader::seekTime(double seconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return false;
    uint64_t ticks = static_cast<uint64_t>(std::max(0.0, seconds) * 90000.0);
    return bd_seek_time(bd_, ticks) >= 0;
}

bool BlurayReader::seekChapter(int chapterIndex) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_ || chapterIndex < 0 || chapterIndex >= chapterCount_) return false;
    return bd_seek_chapter(bd_, static_cast<unsigned>(chapterIndex)) >= 0;
}

int BlurayReader::chapterCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return chapterCount_;
}

int BlurayReader::currentChapter() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return 0;
    return static_cast<int>(bd_get_current_chapter(bd_));
}

double BlurayReader::chapterStartTime(int chapterIndex) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (chapterIndex >= 0 && chapterIndex < static_cast<int>(chapters_.size())) {
        return chapters_[chapterIndex].startTime;
    }
    return 0.0;
}

const std::vector<BlurayChapter>& BlurayReader::chapters() const {
    return chapters_;
}

int BlurayReader::titleCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(titles_.size());
}

int BlurayReader::activeTitle() const {
    return activeTitle_;
}

const std::vector<BlurayTitle>& BlurayReader::titles() const {
    return titles_;
}

double BlurayReader::duration() const {
    return duration_;
}

double BlurayReader::currentTime() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return 0.0;
    int64_t ticks = bd_tell_time(bd_);
    return ticks >= 0 ? static_cast<double>(ticks) / 90000.0 : 0.0;
}

std::string BlurayReader::discTitle() const {
    return discTitle_;
}

bool BlurayReader::isOpen() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bd_ != nullptr;
}

bool BlurayReader::isEncrypted() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return false;
    const BLURAY_DISC_INFO* discInfo = bd_get_disc_info(bd_);
    if (discInfo) {
        if (discInfo->aacs_detected || discInfo->bdplus_detected ||
            discInfo->aacs_error_code != 0) {
            return true;
        }
    }
    return false;
}

bool BlurayReader::encryptionHandled() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bd_) return true;
    const BLURAY_DISC_INFO* discInfo = bd_get_disc_info(bd_);
    if (discInfo) {
        if (discInfo->aacs_detected && (!discInfo->aacs_handled || !discInfo->libaacs_detected)) return false;
        if (discInfo->bdplus_detected && (!discInfo->bdplus_handled || !discInfo->libbdplus_detected)) return false;
        if (discInfo->aacs_error_code != 0) return false;
    }
    return true;
}

#else

bool BlurayReader::open(const std::filesystem::path&, std::string& error) {
    error = "Blu-ray playback is not enabled in this build.";
    return false;
}

bool BlurayReader::selectTitle(int, std::string& error) {
    error = "Blu-ray playback is not enabled in this build.";
    return false;
}

void BlurayReader::close() {}

int BlurayReader::read(uint8_t*, int) { return -1; }

int64_t BlurayReader::seek(int64_t, int) { return -1; }

int64_t BlurayReader::bytePosition() const { return -1; }

bool BlurayReader::seekTime(double) { return false; }

bool BlurayReader::seekChapter(int) { return false; }

int BlurayReader::chapterCount() const { return 0; }

int BlurayReader::currentChapter() const { return 0; }

double BlurayReader::chapterStartTime(int) const { return 0.0; }

static const std::vector<BlurayChapter> emptyChapters;
const std::vector<BlurayChapter>& BlurayReader::chapters() const { return emptyChapters; }

int BlurayReader::titleCount() const { return 0; }

int BlurayReader::activeTitle() const { return -1; }

static const std::vector<BlurayTitle> emptyTitles;
const std::vector<BlurayTitle>& BlurayReader::titles() const { return emptyTitles; }

double BlurayReader::duration() const { return 0.0; }

double BlurayReader::currentTime() const { return 0.0; }

std::string BlurayReader::discTitle() const { return ""; }

bool BlurayReader::isOpen() const { return false; }

bool BlurayReader::isEncrypted() const { return false; }

bool BlurayReader::encryptionHandled() const { return true; }

#endif

#ifdef RENDEPTH_ENABLE_FFMPEG

int BlurayReader::readPacket(void* opaque, uint8_t* buf, int buf_size) {
    auto* reader = static_cast<BlurayReader*>(opaque);
    if (!reader) return AVERROR_EOF;
    int bytesRead = reader->read(buf, buf_size);
    if (bytesRead == 0) return AVERROR_EOF;
    if (bytesRead < 0) return AVERROR(EIO);
    return bytesRead;
}

int64_t BlurayReader::seekPacket(void* opaque, int64_t offset, int whence) {
    auto* reader = static_cast<BlurayReader*>(opaque);
    if (!reader) return AVERROR(EIO);
    int64_t result = reader->seek(offset, whence);
    if (result < 0) return AVERROR(EIO);
    return result;
}

AVIOContext* BlurayReader::createAVIOContext(int bufferSize) {
    if (!isOpen()) return nullptr;
    auto* buffer = static_cast<unsigned char*>(av_malloc(static_cast<size_t>(bufferSize)));
    if (!buffer) return nullptr;

    AVIOContext* avio = avio_alloc_context(
        buffer,
        bufferSize,
        0, // write_flag = 0 (read-only)
        this,
        &BlurayReader::readPacket,
        nullptr,
        &BlurayReader::seekPacket
    );

    if (!avio) {
        av_free(buffer);
        return nullptr;
    }

    avio->seekable |= AVIO_SEEKABLE_NORMAL;

    return avio;
}

#endif
