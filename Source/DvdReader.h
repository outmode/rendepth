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

#ifdef RENDEPTH_ENABLE_DVD
extern "C" {
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_types.h>
#include <dvdread/ifo_read.h>
}
#else
typedef struct dvd_reader_s dvd_reader_t;
typedef struct dvd_file_s dvd_file_t;
typedef struct ifo_handle_s ifo_handle_t;
#endif

struct DvdChapter {
    int index = 0;
    double startTime = 0.0; // seconds
    double duration = 0.0;  // seconds
    int startCell = 0;
    uint32_t startSector = 0;
    std::string name;
};

struct DvdTitle {
    int titleNum = 0;      // 1-based overall title number
    int titleSetNum = 0;   // 1-based VTS number
    int vtsTitleNum = 0;   // title number within VTS
    double duration = 0.0; // seconds
    int chapterCount = 0;
    int angleCount = 0;
    std::vector<DvdChapter> chapters;
};

class DvdReader {
public:
    DvdReader();
    ~DvdReader();

    static bool isDvdSource(const std::filesystem::path& path);
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
    const std::vector<DvdChapter>& chapters() const;

    int titleCount() const;
    int activeTitle() const;
    bool selectTitle(int titleIndex, std::string& error);
    const std::vector<DvdTitle>& titles() const;

    double duration() const;
    double currentTime() const;
    std::string discTitle() const;
    bool isOpen() const;
    bool isEncrypted() const;

#ifdef RENDEPTH_ENABLE_FFMPEG
    AVIOContext* createAVIOContext(int bufferSize = 65536);
    static int readPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seekPacket(void* opaque, int64_t offset, int whence);
#endif

private:
#ifdef RENDEPTH_ENABLE_DVD
    dvd_reader_t* dvd_ = nullptr;
    dvd_file_t* vtsFile_ = nullptr;
    ifo_handle_t* vmgIfo_ = nullptr;
    ifo_handle_t* vtsIfo_ = nullptr;
    mutable std::recursive_mutex mutex_;
    int activeTitle_ = -1;
    int activeVts_ = -1;
    double duration_ = 0.0;
    int chapterCount_ = 0;
    int currentChapter_ = 0;
    int64_t currentByteOffset_ = 0;
    int64_t totalSizeBytes_ = 0;
    std::string discTitle_;
    std::filesystem::path discPath_;
    std::vector<DvdTitle> titles_;
    std::vector<DvdChapter> chapters_;

    void inspectTitles();
#endif
};
