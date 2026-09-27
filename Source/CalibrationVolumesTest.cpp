#include "CalibrationVolumes.h"
#include <cassert>
#include <cstdio>
#ifdef __APPLE__
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#endif

// Verify mount parsing excludes optical media from calibration-file searches.
int main(int argc, char** argv) {
#ifdef __linux__
    FILE* mounts = std::tmpfile();
    assert(mounts);
    std::fputs(
        "/dev/sr0 /run/media/user/BDROM udf ro 0 0\n"
        "/dev/sr1 /media/Audio\\040Disc iso9660 ro 0 0\n"
        "/dev/sdi /run/media/user/LKG-E04406 vfat ro 0 0\n"
        "/dev/nvme0n1p1 /home ext4 rw 0 0\n", mounts);
    std::rewind(mounts);
    const auto optical = CalibrationVolumes::opticalMounts(mounts);
    std::fclose(mounts);
    assert(optical.size() == 2);
    assert(CalibrationVolumes::contains(optical, "/run/media/user/BDROM"));
    assert(CalibrationVolumes::contains(optical, "/run/media/user/BDROM/LKG_calibration/visual.json"));
    assert(CalibrationVolumes::contains(optical, "/media/Audio Disc"));
    assert(!CalibrationVolumes::contains(optical, "/run/media/user/BDROM-backup"));
    assert(!CalibrationVolumes::contains(optical, "/run/media/user/LKG-E04406"));
    assert(!CalibrationVolumes::contains(optical, "/run/media/user"));
    assert(!CalibrationVolumes::contains(optical, "/home/user/photo.jpg"));
    assert(!CalibrationVolumes::contains(optical, "/media/Audio Disc/../LKG"));
    for (const auto& mount : CalibrationVolumes::opticalMounts())
        std::printf("Excluded optical mount (metadata only): %s\n", mount.c_str());
#endif
#ifdef __APPLE__
    char directory[] = "/private/tmp/rendepth-calibration-XXXXXX";
    assert(mkdtemp(directory));
    const auto root = std::filesystem::path(directory);
    const auto unrelated = root / "BDROM" / "LKG_calibration" / "visual.json";
    const auto device = root / "LKG-E12345" / "LKG_calibration" / "visual.json";
    std::filesystem::create_directories(unrelated.parent_path());
    std::filesystem::create_directories(device.parent_path());
    std::ofstream(unrelated).put('\n');
    std::ofstream(device).put('\n');
    assert(CalibrationVolumes::lookingGlassCalibration(root) == device);
    std::filesystem::remove_all(root);
    if (argc > 1) {
        const auto mounted = CalibrationVolumes::lookingGlassCalibration(argv[1]);
        assert(!mounted.empty());
        std::printf("Mounted Looking Glass calibration: %s\n", mounted.c_str());
    }
#endif
    std::puts("Calibration volume exclusion tests passed");
}
