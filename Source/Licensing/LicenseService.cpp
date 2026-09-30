#include "LicenseDialog.h"
#include "LicenseManager.h"
#include "LicenseConfig.h"
#include <future>
#include <memory>
namespace Licensing::Service {
namespace {
std::unique_ptr<LicenseManager> manager;
std::future<Result> pending;
DialogState state;
// Translate the stored license result into dialog state and a default status message.
void apply(const Result& result) {
    state.licensed = result.record.complete();
    state.maskedKey.clear();
    if (state.licensed) {
        for (char character : result.record.key) {
            if (character == '-') state.maskedKey += '-';
            else state.maskedKey += "\xE2\x97\x8F"; // U+25CF BLACK CIRCLE
        }
    }
    state.message = result.message;
    if (state.message.empty()) state.message = state.licensed ? "Rendepth Pro is activated on this computer." :
        "Enter the license key from your purchase email.";
}
// Launch activation or deactivation asynchronously while marking the dialog busy.
void action(bool deactivate, std::string key) {
    if (!manager || state.busy) return;
    state.busy = true;
    state.message = deactivate ? "Deactivating this computer..." : "Activating Rendepth Pro...";
    NativeDialog::update(state);
    pending = std::async(std::launch::async, [deactivate, key = std::move(key)] {
        try { return deactivate ? manager->deactivate() : manager->activate(key); }
        catch (...) { return Result{Status::StorageError, "The license operation could not be completed. Please contact support."}; }
    });
}
}
// Initialize licensing from build configuration and the saved per-user activation record.
void initialize() {
    if (manager) return;
    const auto config = buildConfig();
    state.canActivate = config.configured();
    state.purchaseUrl = config.purchaseUrl; state.supportUrl = config.supportUrl;
    char* pref = SDL_GetPrefPath("Outmode", "Rendepth");
    const auto path = pref ? std::filesystem::path(pref) / "License.json" : std::filesystem::path{};
    SDL_free(pref);
    manager = std::make_unique<LicenseManager>(config, path);
    apply(manager->load());
    if (!state.canActivate && !state.licensed) state.message = "Pro activation is not available in this build yet.";
}
// Refresh saved licensing state and open or focus the native license dialog.
void open(SDL_Window* parent) {
    initialize();
    if (!state.busy) {
        apply(manager->load());
        if (!state.canActivate && !state.licensed) state.message = "Pro activation is not available in this build yet.";
    }
    NativeDialog::open(parent, state, action);
}
// Consume completed license operations, refresh authoritative local state, and service the dialog.
bool poll() {
    const bool before = state.licensed;
    if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = pending.get();
        // Reload authoritative local state, including on network/storage failure.
        apply(manager->load());
        if (!result.message.empty()) state.message = result.message;
        state.busy = false;
        NativeDialog::update(state);
    }
    NativeDialog::poll();
    return before != state.licensed;
}
// Allow an in-flight license operation to finish before closing its dialog and manager.
void close() {
    // Let an in-flight activation finish and persist even if the user quits.
    if (pending.valid()) pending.wait();
    NativeDialog::close();
    manager.reset();
}
// Report the locally established licensing state used by feature checks.
bool isLicensed() { return state.licensed; }
}
