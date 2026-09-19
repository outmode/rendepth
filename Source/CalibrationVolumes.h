#pragma once
#include <filesystem>
#include <vector>
#ifdef __linux__
#include <cstdio>
#include <cstring>
#include <mntent.h>
#endif

namespace CalibrationVolumes {
// Read mount metadata, never the mounted media. Even stat() on an optical
// volume can wake the drive; exclude it before inspecting calibration files.
#ifdef __linux__
inline std::vector<std::filesystem::path> opticalMounts(FILE* mounts) {
    std::vector<std::filesystem::path> paths;
    if (!mounts) return paths;
    mntent entry{};
    char buffer[16384];
    while (getmntent_r(mounts, &entry, buffer, sizeof(buffer))) {
        if (std::strcmp(entry.mnt_type, "udf") == 0 ||
            std::strcmp(entry.mnt_type, "iso9660") == 0 ||
            std::strncmp(entry.mnt_fsname, "/dev/sr", 7) == 0 ||
            std::strncmp(entry.mnt_fsname, "/dev/cdrom", 10) == 0 ||
            std::strncmp(entry.mnt_fsname, "/dev/dvd", 8) == 0)
            paths.emplace_back(entry.mnt_dir);
    }
    return paths;
}
#endif
inline std::vector<std::filesystem::path> opticalMounts() {
#ifdef __linux__
    FILE* mounts = setmntent("/proc/self/mounts", "r");
    auto paths = opticalMounts(mounts);
    if (mounts) endmntent(mounts);
    return paths;
#else
    return {};
#endif
}
inline bool contains(const std::vector<std::filesystem::path>& mounts,
                     const std::filesystem::path& path) {
    const auto normalized = path.lexically_normal();
    for (const auto& mount : mounts) {
        const auto relative = normalized.lexically_relative(mount);
        if (!relative.empty() && *relative.begin() != "..") return true;
    }
    return false;
}
}
