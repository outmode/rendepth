#include "DiscSource.h"
#include "AudioCdReader.h"
#include "BlurayReader.h"
#include "DvdReader.h"
#include <algorithm>

namespace DiscSource {
namespace {
// Normalize disc paths and extensions for case-insensitive identification.
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}
// Recognize extracted Blu-ray and DVD directory layouts, including selected metadata files.
Type directoryType(std::filesystem::path path) {
    std::error_code ec;
    const auto name = lower(path.filename().string());
    if (name == "index.bdmv" || name == "movieobject.bdmv" || lower(path.extension().string()) == ".ifo") path = path.parent_path();
    if (lower(path.filename().string()) == "bdmv" || lower(path.filename().string()) == "video_ts") path = path.parent_path();
    for (const auto* dir : {"BDMV", "bdmv"})
        for (const auto* file : {"index.bdmv", "INDEX.BDMV"})
            if (std::filesystem::is_regular_file(path / dir / file, ec)) return Type::Bluray;
    for (const auto* dir : {"VIDEO_TS", "video_ts"})
        for (const auto* file : {"VIDEO_TS.IFO", "video_ts.ifo"})
            if (std::filesystem::is_regular_file(path / dir / file, ec)) return Type::Dvd;
    return Type::None;
}
}
// Identify paths worth probing as disc folders, ISO images, or optical drives.
bool candidate(const std::filesystem::path& path) {
    return directoryType(path) != Type::None || lower(path.extension().string()) == ".iso" ||
        !AudioCdReader::devicePath(path).empty();
}
// Determine the disc type by inspecting its directory layout or probing the available disc backends.
Type detect(const std::filesystem::path& path) {
    if (const auto type = directoryType(path); type != Type::None) return type;
    const auto device = AudioCdReader::devicePath(path);
    if (!device.empty() && AudioCdReader::isAudioCd(path)) return Type::AudioCd;
    if (device.empty() && lower(path.extension().string()) != ".iso") return Type::None;
    const auto source = device.empty() ? path : device;
#ifdef RENDEPTH_ENABLE_BLURAY
    if (auto* bd = bd_open(source.string().c_str(), nullptr)) {
        const auto* info = bd_get_disc_info(bd);
        const bool detected = info && info->bluray_detected;
        bd_close(bd);
        if (detected) return Type::Bluray;
    }
#endif
#ifdef RENDEPTH_ENABLE_DVD
    if (auto* dvd = DVDOpen(source.string().c_str())) {
        auto* ifo = ifoOpenVMGI(dvd);
        const bool detected = ifo != nullptr;
        if (ifo) ifoClose(ifo);
        DVDClose(dvd);
        if (detected) return Type::Dvd;
    }
#endif
    return Type::None;
}
}
