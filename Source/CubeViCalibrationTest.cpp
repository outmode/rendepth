#include "CubeViCalibration.h"
#include "NativeDisplayIdentity.h"
#include <cmath>
#include <cstdio>
#include <string>

// Check CubeVi calibration parsing, optical validation, display identification, and optional local
// integration.
int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    CubeViCalibration::Optics optics;
    std::string error;
    check(CubeViCalibration::parse(
        R"({"config":{"lineNumber":20,"obliquity":0.125,"deviation":8,"originDeviation":4}})",
        optics, error), "decoded wrapper");
    check(optics.interval == 20 && optics.obliquity == 0.125f && optics.deviation == 8,
        "use adjusted deviation, not originDeviation");
    check(CubeViCalibration::parse(
        R"({"lineNumber":20,"obliquity":-0.125,"deviation":0})", optics, error),
        "standalone optical object, negative slope and zero deviation");
    for (const char* key : {"line_number", "line number", "LineNumber"}) {
        const std::string legacy = std::string("{\"") + key +
            "\":{\"value\":20},\"Obliquity\":{\"value\":0.125},\"Deviation\":8}";
        check(CubeViCalibration::parse(legacy, optics, error), "legacy screen_params field aliases");
        check(optics.interval == 20 && optics.obliquity == 0.125f && optics.deviation == 8,
            "legacy fields use the same CubeVi optical model");
    }
    for (const char* invalid : {"", "null", "[]", "{", R"({"config":[]})",
        R"({"lineNumber":0,"obliquity":0.1,"deviation":8})",
        R"({"lineNumber":-20,"obliquity":0.1,"deviation":8})",
        R"({"lineNumber":"20","obliquity":0.1,"deviation":8})",
        R"({"lineNumber":20,"obliquity":0.1})",
        R"({"lineNumber":20,"obliquity":1e100,"deviation":8})",
        R"({"config":"invalid base64!"})",
        R"({"config":"U2FsdGVkX18BAgMEBQYHCA=="})"}) {
        const auto before = optics;
        check(!CubeViCalibration::parse(invalid, optics, error), "reject invalid calibration");
        check(!error.empty(), "failure explains the error");
        check(optics.interval == before.interval && optics.obliquity == before.obliquity &&
            optics.deviation == before.deviation, "failed parse preserves previous optics");
    }
    check(!CubeViCalibration::parse(std::string(65537, ' '), optics, error), "file size limit");
#ifdef _WIN32
    // Independent .NET AES fixture with fixed salt 01..08 and synthetic optics.
    check(CubeViCalibration::parse(
        R"({"config":"U2FsdGVkX18BAgMEBQYHCEvLKQ2NUxEsgFsgk+8hT5NlNKHVu9OpV8qr/UHk4m5m4Sl+HrIe2OAscTCmGuVuuVZcRHGrK3OKno2bSEUzz3g="})",
        optics, error), "encrypted vendor envelope");
    check(optics.interval == 20 && optics.obliquity == 0.125f && optics.deviation == 8,
        "encrypted fixture optical values");
#endif
    check(NativeDisplayIdentity::isCubeViC1("MONITOR\\OPC1155\\example", 1440, 2560),
        "observed C1 hardware model");
    check(!NativeDisplayIdentity::isCubeViC1("MONITOR\\OPC11550\\example", 1440, 2560),
        "no partial hardware model match");
    check(!NativeDisplayIdentity::isCubeViC1("MONITOR\\SAM7859\\example", 1440, 2560),
        "resolution is not device identity");
    check(!NativeDisplayIdentity::isCubeViC1("C1", 1440, 2560), "no speculative C1 name match");
    check(!NativeDisplayIdentity::isCubeViC1("MONITOR\\OPC1155\\example", 2560, 1440),
        "rotated desktop must not silently use portrait optics");

    // Optional local integration check. No device calibration is embedded in tests.
    if (argc > 1) {
        check(CubeViCalibration::load(std::filesystem::path(argv[1]), optics, error),
            error.c_str());
        if (error.empty()) std::printf("Local calibration: %.6f %.6f %.6f\n",
            optics.interval, optics.obliquity, optics.deviation);
        check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization");
        int count = 0;
        auto* displays = SDL_GetDisplays(&count);
        for (int i = 0; displays && i < count; ++i) {
            const auto hardware = NativeDisplayIdentity::hardwareId(displays[i]);
            const auto* mode = SDL_GetCurrentDisplayMode(displays[i]);
            const char* name = SDL_GetDisplayName(displays[i]);
            std::printf("Display: %s | %s | %dx%d | CubeVi=%d\n",
                SDL_GetDisplayName(displays[i]), hardware.c_str(), mode ? mode->w : 0,
                mode ? mode->h : 0, NativeDisplayIdentity::isCubeViC1(hardware,
                    mode ? mode->w : 0, mode ? mode->h : 0, name ? name : ""));
        }
        SDL_free(displays);
        SDL_Quit();
    }
    std::printf("CubeVi calibration tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
