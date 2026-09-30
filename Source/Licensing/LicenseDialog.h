#pragma once
#include "LicenseTypes.h"
#include <SDL3/SDL.h>
#include <functional>
namespace Licensing {
// Native windows are driven from SDL's main thread. Network work lives in Service.
struct DialogState {
    bool licensed = false, busy = false, canActivate = false;
    std::string message, purchaseUrl, supportUrl;
    std::string maskedKey;
};
using DialogAction = std::function<void(bool deactivate, std::string key)>;
namespace NativeDialog {
void open(SDL_Window* parent, const DialogState& state, DialogAction action);
void update(const DialogState& state);
void poll();
void close();
}
namespace Service {
void initialize();
void open(SDL_Window* parent);
bool poll(); // true when license state changes
void close();
bool isLicensed();
}
}
