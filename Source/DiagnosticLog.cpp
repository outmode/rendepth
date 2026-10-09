// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "DiagnosticLog.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>

namespace {
std::mutex logMutex;
std::ofstream logFile;
const auto started = std::chrono::steady_clock::now();

void appendSafe(std::string& line, std::string_view text) {
    for (unsigned char ch : text) {
        if (ch >= 32 && ch < 127) line += static_cast<char>(ch);
        else line += '?';
    }
}
}

namespace DiagnosticLog {
void start(const std::filesystem::path& settingsDirectory) {
    std::lock_guard lock(logMutex);
    std::error_code error;
    std::filesystem::create_directories(settingsDirectory, error);
    if (error) return;
    const auto path = settingsDirectory / "error_log.txt";
    constexpr std::uintmax_t maxLogBytes = 2 * 1024 * 1024;
    constexpr std::streamoff retainedBytes = 1024 * 1024;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > maxLogBytes) {
        std::ifstream previous(path, std::ios::binary);
        previous.seekg(-retainedBytes, std::ios::end);
        std::string tail(static_cast<size_t>(retainedBytes), '\0');
        previous.read(tail.data(), retainedBytes);
        const bool complete = previous.gcount() == retainedBytes;
        previous.close();
        const auto firstNewline = tail.find('\n');
        if (firstNewline != std::string::npos) tail.erase(0, firstNewline + 1);
        if (complete) {
            std::ofstream trimmed(path, std::ios::binary | std::ios::trunc);
            trimmed << tail;
        }
    }
    logFile.open(path, std::ios::app);
    if (!logFile) return;
    logFile << "\n=== Rendepth session ===\n";
    logFile.flush();
}

void write(std::string_view level, std::string_view event, std::string_view fields) {
    std::lock_guard lock(logMutex);
    if (!logFile) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::string line = std::to_string(elapsed) + "ms ";
    appendSafe(line, level);
    line += ' ';
    appendSafe(line, event);
    if (!fields.empty()) {
        line += ' ';
        appendSafe(line, fields);
    }
    logFile << line << '\n';
    logFile.flush();
}
}
