#pragma once
#include <filesystem>
namespace DiscSource {
enum class Type { None, AudioCd, Dvd, Bluray };
Type detect(const std::filesystem::path& path);
// Cheap admission check; actual drive/image probing happens when opening media.
bool candidate(const std::filesystem::path& path);
}
