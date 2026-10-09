// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string_view>

// Only pass fixed event names and explicitly selected, path-free fields here.
// SDL's general log is deliberately not copied: some existing messages contain paths.
namespace DiagnosticLog {
void start(const std::filesystem::path& settingsDirectory);
void write(std::string_view level, std::string_view event,
    std::string_view fields = {});
}
