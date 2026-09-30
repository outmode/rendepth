// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include "DepthEstimator.h"

// Load exactly one ORT core for the lifetime of the process. Preferences
// can be saved while inference is running, but take effect only after restart.
namespace InferenceRuntime {
using Provider = DepthEstimator::Provider;
void configure(const std::filesystem::path& appRoot,
    const std::filesystem::path& packRoot, Provider preferred);
bool initialize(Provider requested, std::string& error);
Provider provider();
bool installed(Provider backend);
std::filesystem::path packDirectory();
std::string status();
void useCpuFallback(const std::string& reason);
}

#ifdef _WIN32
struct OrtSessionOptions;
namespace InferenceRuntime {
// Resolves the factory from the selected DirectML pack, without an import lib.
// Throws Ort::Exception on failure so the normal CPU session retry applies.
void appendDirectML(OrtSessionOptions* options);
}
#endif
