#pragma once

#include "rapidjson/document.h"
#include <initializer_list>

namespace AIEngineSettings {
// The persistence key is independent of the wording shown in the menu.
inline constexpr char key[] = "AI Engine";

inline int read(const rapidjson::Value& document) {
    if (!document.IsObject()) return -1;
    for (const char* name : {key, "AI Engine (Restart Required)", "AI Inference (Restart Required)"}) {
        if (!document.HasMember(name)) continue;
        const auto& value = document[name];
        // A canonical key takes precedence, even if malformed; do not revive
        // a stale legacy preference when both keys are present.
        return value.IsInt() && value.GetInt() >= 0 && value.GetInt() <= 2 ? value.GetInt() : -1;
    }
    return -1;
}

inline int forSave(int selected, bool explicitlyChanged, const rapidjson::Value& existing) {
    const int saved = read(existing);
    return !explicitlyChanged && saved >= 0 ? saved : selected;
}
}
