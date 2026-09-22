#include "AudioCdReader.h"
#include "DiscSource.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <limits>
#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}
#endif
// Verify virtual WAV reads, seeking, mixed-mode track gaps, and disc-source identification.
int main() {
    int reads = 0;
    AudioCdStream stream;
    // Audio tracks separated by a data track: the data sectors must be skipped.
    assert(stream.open({{1, 0, 750}, {3, 1500, 2250}}, [&](int sector, uint8_t* bytes) {
        assert(sector < 750 || (sector >= 1500 && sector < 2250));
        ++reads; std::fill_n(bytes, 2352, uint8_t(sector)); return true;
    }));
    assert(stream.duration() == 20 && stream.chapters() == std::vector<double>({0, 10}));
    uint8_t bytes[48]{};
    assert(stream.read(bytes, 44) == 44 && reads == 0);
    assert(std::string(reinterpret_cast<char*>(bytes), 4) == "RIFF");
    assert(stream.seek(44 + 750 * 2352 - 2, SEEK_SET) >= 0);
    assert(stream.read(bytes, 4) == 4);
    assert(bytes[0] == uint8_t(749) && bytes[1] == uint8_t(749));
    assert(bytes[2] == uint8_t(1500) && bytes[3] == uint8_t(1500));
    const auto position = stream.seek(0, SEEK_CUR);
    assert(stream.seek(INT64_MAX, SEEK_CUR) == -1 && stream.seek(INT64_MIN, SEEK_CUR) == -1);
    assert(stream.seek(0, SEEK_CUR) == position);
    assert(stream.seek(-1, SEEK_END) > 0 && stream.read(bytes, 48) == 1 && stream.read(bytes, 1) == 0);
#ifdef RENDEPTH_ENABLE_FFMPEG
    stream.seek(0, SEEK_SET);
    auto* io = stream.createAVIOContext(); assert(io);
    auto* format = avformat_alloc_context(); assert(format);
    format->pb = io; format->flags |= AVFMT_FLAG_CUSTOM_IO;
    assert(avformat_open_input(&format, nullptr, av_find_input_format("wav"), nullptr) == 0);
    assert(avformat_find_stream_info(format, nullptr) >= 0);
    assert(format->nb_streams == 1 && format->streams[0]->codecpar->sample_rate == 44100);
    assert(format->streams[0]->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE);
    auto* packet = av_packet_alloc();
    for (int second : {15, 2, 10, 19, 0}) {
        assert(av_seek_frame(format, 0, int64_t(second) * 44100, AVSEEK_FLAG_BACKWARD) >= 0);
        assert(av_read_frame(format, packet) >= 0);
        assert(packet->pts == int64_t(second) * 44100);
        const int sector = second < 10 ? second * 75 : 1500 + (second - 10) * 75;
        assert(packet->data[0] == uint8_t(sector));
        av_packet_unref(packet);
    }
    av_packet_free(&packet); avformat_close_input(&format);
    av_freep(&io->buffer); avio_context_free(&io);
#endif
    AudioCdStream bad;
    assert(!bad.open({}, {}));
    assert(!bad.open({{1, 10, 5}}, [](int, uint8_t*) { return true; }));
    assert(!bad.open({{1, 0, 100}, {2, 50, 150}}, [](int, uint8_t*) { return true; }));
    assert(bad.open({{1, 0, 75}}, [](int, uint8_t*) { return false; }));
    assert(bad.read(bytes, 44) == 44 && bad.read(bytes, 1) == -1);
#ifdef __linux__
    assert(AudioCdReader::devicePath("cdda://sr0/") == "/dev/sr0");
    assert(AudioCdReader::devicePath("/run/user/1000/gvfs/cdda:host=sr2/Track 1.wav") == "/dev/sr2");
    assert(AudioCdReader::devicePath("cdda://sr0/../../sda") == "/dev/sr0");
    assert(AudioCdReader::devicePath("cdda://sda/").empty());
#endif
    auto root = std::filesystem::temp_directory_path() / ("rendepth-disc-signatures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "BDMV");
    assert(DiscSource::detect(root) == DiscSource::Type::None);
    std::ofstream(root / "BDMV/index.bdmv").put('\0');
    assert(DiscSource::detect(root) == DiscSource::Type::Bluray);
    std::filesystem::remove_all(root / "BDMV");
    std::filesystem::create_directories(root / "VIDEO_TS");
    std::ofstream(root / "VIDEO_TS/VIDEO_TS.IFO").put('\0');
    assert(DiscSource::detect(root) == DiscSource::Type::Dvd);
    assert(DiscSource::detect(root / "VIDEO_TS/VIDEO_TS.IFO") == DiscSource::Type::Dvd);
    std::filesystem::remove_all(root);
    std::puts("Audio CD stream, seek, mixed-mode and disc signature tests passed");
}
