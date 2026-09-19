#include "SettingsFile.h"
#include "AIEngineSettings.h"
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include <csignal>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

static bool interruptBeforeCommit = false;
static int interruptSignal = -1;
extern "C" int __real_fsync(int fd);
extern "C" int __wrap_fsync(int fd) {
    if (interruptBeforeCommit) {
        assert(::write(interruptSignal, "x", 1) == 1);
        for (;;) pause();
    }
    return __real_fsync(fd);
}
static rapidjson::Document read(const std::filesystem::path& path) {
    std::ifstream file(path);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    rapidjson::Document document;
    document.Parse(text.c_str());
    assert(!document.HasParseError() && document.IsObject());
    return document;
}
static void save(const std::filesystem::path& path, int choice, bool explicitChange) {
    std::string error;
    SettingsFile transaction(path, error);
    assert(transaction.locked());
    auto previous = read(path);
    const int engine = AIEngineSettings::forSave(choice, explicitChange, previous);
    assert(transaction.write("{\"AI Engine\":" + std::to_string(engine) + "}", error));
}
static void waitSuccess(pid_t pid) {
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("rendepth-settings-test-" + std::to_string(getpid()));
    const auto path = root / "Settings.json";
    std::string error;
    {
        SettingsFile file(path, error); assert(file.locked());
        assert(file.write("{\"AI Engine\":0}", error));
    }
    int ready[2], release[2], done[2];
    assert(pipe(ready) == 0 && pipe(release) == 0 && pipe(done) == 0);
    pid_t stale = fork(); assert(stale >= 0);
    if (stale == 0) {
        {
            SettingsFile transaction(path, error); assert(transaction.locked());
            auto previous = read(path);
            const int engine = AIEngineSettings::forSave(0, false, previous);
            assert(::write(ready[1], "x", 1) == 1);
            char byte; assert(::read(release[0], &byte, 1) == 1);
            assert(transaction.write("{\"AI Engine\":" + std::to_string(engine) + "}", error));
        }
        _exit(0);
    }
    char byte; assert(::read(ready[0], &byte, 1) == 1);
    pid_t selection = fork(); assert(selection >= 0);
    if (selection == 0) {
        save(path, 2, true);
        assert(::write(done[1], "x", 1) == 1);
        _exit(0);
    }
    pollfd pending{done[0], POLLIN, 0};
    assert(poll(&pending, 1, 100) == 0); // New selection cannot race the stale transaction.
    assert(::write(release[1], "x", 1) == 1);
    waitSuccess(stale); waitSuccess(selection);
    assert(AIEngineSettings::read(read(path)) == 2);
    save(path, 0, false); // A later autosave from the old CPU instance preserves ROCm.
    assert(AIEngineSettings::read(read(path)) == 2);

    pid_t writer = fork(); assert(writer >= 0);
    if (writer == 0) {
        for (int i = 0; i < 100; ++i) save(path, 0, false);
        _exit(0);
    }
    for (int i = 0; i < 2000; ++i) assert(AIEngineSettings::read(read(path)) == 2);
    waitSuccess(writer); // Unlocked startup readers never see a truncated document.

    pid_t interrupted = fork(); assert(interrupted >= 0);
    if (interrupted == 0) {
        interruptBeforeCommit = true; interruptSignal = ready[1];
        save(path, 0, true); _exit(1);
    }
    assert(::read(ready[0], &byte, 1) == 1); // Temp file written, original not replaced yet.
    assert(kill(interrupted, SIGKILL) == 0);
    int status = 0; assert(waitpid(interrupted, &status, 0) == interrupted && WIFSIGNALED(status));
    assert(AIEngineSettings::read(read(path)) == 2);
    save(path, 0, false); // Crash releases the transaction lock.
    assert(AIEngineSettings::read(read(path)) == 2);
    save(path, 0, true); // An intentional CPU selection must still work.
    assert(AIEngineSettings::read(read(path)) == 0);
    for (int fd : {ready[0], ready[1], release[0], release[1], done[0], done[1]}) close(fd);
    std::filesystem::remove_all(root);
    std::puts("PASS: concurrent selections/autosaves, atomic startup reads, interrupted save, lock recovery, explicit CPU selection");
}
