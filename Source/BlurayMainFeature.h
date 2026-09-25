#pragma once

#include "BlurayReader.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace BlurayMainFeature {

inline int languageCount(const BlurayTitle& title) {
    std::set<std::string> languages(title.audioLanguages.begin(), title.audioLanguages.end());
    for (const auto& stream : title.audioStreamLanguages)
        if (!stream.second.empty() && stream.second != "und") languages.insert(stream.second);
    for (const auto& stream : title.subtitleStreamLanguages)
        if (!stream.second.empty() && stream.second != "und") languages.insert(stream.second);
    return static_cast<int>(languages.size());
}

// Compare language coverage only among titles within a second of the longest
// playlist. This distinguishes alternate full-length audio cuts without letting
// a well-localized short extra displace the feature.
inline int select(const std::vector<BlurayTitle>& titles) {
    if (titles.empty()) return -1;
    const double longest = std::max_element(titles.begin(), titles.end(),
        [](const auto& a, const auto& b) { return a.duration < b.duration; })->duration;
    const BlurayTitle* best = nullptr;
    for (const auto& title : titles) {
        if (longest - title.duration >= 1.0) continue;
        if (!best || languageCount(title) > languageCount(*best) ||
            (languageCount(title) == languageCount(*best) &&
                (title.chapterCount > best->chapterCount ||
                    (title.chapterCount == best->chapterCount &&
                        title.mainFeatureCandidate && !best->mainFeatureCandidate))))
            best = &title;
    }
    return best ? best->index : titles.front().index;
}

} // namespace BlurayMainFeature
