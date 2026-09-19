#include "SettingsFile.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
std::string systemError() {
#ifdef _WIN32
    return std::system_category().message(GetLastError());
#else
    return std::generic_category().message(errno);
#endif
}
}
SettingsFile::SettingsFile(const std::filesystem::path& path, std::string& error) : path_(path) {
    error.clear();
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = ec.message(); return; }
    auto lockPath = path;
    lockPath += ".lock";
#ifdef _WIN32
    lock_ = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock_ == INVALID_HANDLE_VALUE) { error = systemError(); lock_ = nullptr; return; }
    OVERLAPPED range{};
    locked_ = LockFileEx(lock_, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &range) != 0;
#else
    lock_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_ < 0) { error = systemError(); return; }
    int result;
    do { result = flock(lock_, LOCK_EX); } while (result < 0 && errno == EINTR);
    locked_ = result == 0;
#endif
    if (!locked_) error = systemError();
}
SettingsFile::~SettingsFile() {
#ifdef _WIN32
    if (lock_) CloseHandle(lock_);
#else
    if (lock_ >= 0) ::close(lock_);
#endif
    // Do not unlink the lock file: another instance may already be waiting on it.
}
bool SettingsFile::write(std::string_view contents, std::string& error) {
    error.clear();
    if (!locked_) { error = "Settings transaction is not locked"; return false; }
#ifdef _WIN32
    static std::atomic<unsigned long long> serial{0};
    auto temporary = path_;
    temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
        std::to_string(GetTickCount64()) + "." + std::to_string(serial++);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = systemError(); return false; }
    DWORD written = 0;
    bool ok = contents.size() <= MAXDWORD && WriteFile(file, contents.data(),
        static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size();
    if (ok) ok = FlushFileBuffers(file) != 0;
    if (!ok) error = systemError();
    if (!CloseHandle(file) && ok) { error = systemError(); ok = false; }
    if (ok && !MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = systemError(); ok = false;
    }
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
#else
    auto pattern = path_.string() + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const int file = mkstemp(name.data());
    if (file < 0) { error = systemError(); return false; }
    bool ok = true;
    size_t offset = 0;
    while (offset < contents.size()) {
        const auto written = ::write(file, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { error = systemError(); ok = false; break; }
        offset += static_cast<size_t>(written);
    }
    if (ok && fsync(file) != 0) { error = systemError(); ok = false; }
    if (::close(file) != 0 && ok) { error = systemError(); ok = false; }
    if (ok && ::rename(name.data(), path_.c_str()) != 0) { error = systemError(); ok = false; }
    if (!ok) ::unlink(name.data());
    return ok;
#endif
}
