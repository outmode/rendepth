#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>

#ifdef RENDEPTH_ENABLE_FFMPEG
struct AVIOContext;
#endif

#ifdef RENDEPTH_ENABLE_BLURAY
extern "C" {
#include <libbluray/bluray.h>
}
#else
typedef struct bd_disc_s BLURAY;
#endif

struct BlurayChapter {
    int index = 0;
    double startTime = 0.0; // seconds
    double duration = 0.0;  // seconds
    std::string name;
};

struct BlurayTitle {
    int index = 0;
    uint32_t playlist = 0;
    double duration = 0.0;  // seconds
    int chapterCount = 0;
    int angleCount = 0;
    int audioTrackCount = 0;
    std::vector<BlurayChapter> chapters;
};

class BlurayReader {
public:
    BlurayReader();
    ~BlurayReader();

    static bool isBluraySource(const std::filesystem::path& path);
    static std::filesystem::path resolveDiscRoot(const std::filesystem::path& path);

    bool open(const std::filesystem::path& path, std::string& error);
    void close();

    int read(uint8_t* buffer, int size);
    int64_t seek(int64_t offset, int whence);

    int64_t bytePosition() const;
    bool seekTime(double seconds);
    bool seekChapter(int chapterIndex);

    int chapterCount() const;
    int currentChapter() const;
    double chapterStartTime(int chapterIndex) const;
    const std::vector<BlurayChapter>& chapters() const;

    int titleCount() const;
    int activeTitle() const;
    bool selectTitle(int titleIndex, std::string& error);
    const std::vector<BlurayTitle>& titles() const;

    double duration() const;
    double currentTime() const;
    std::string discTitle() const;
    bool isOpen() const;
    bool isEncrypted() const;
    bool encryptionHandled() const;

#ifdef RENDEPTH_ENABLE_FFMPEG
    AVIOContext* createAVIOContext(int bufferSize = 32768);
    static int readPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seekPacket(void* opaque, int64_t offset, int whence);
#endif

private:
#ifdef RENDEPTH_ENABLE_BLURAY
    BLURAY* bd_ = nullptr;
    mutable std::recursive_mutex mutex_;
    int activeTitle_ = -1;
    double duration_ = 0.0;
    int chapterCount_ = 0;
    std::string discTitle_;
    std::filesystem::path discPath_;
    std::vector<BlurayTitle> titles_;
    std::vector<BlurayChapter> chapters_;

    void inspectTitles();
#endif
};
