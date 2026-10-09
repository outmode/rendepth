#include "MediaOpenDialog.h"
#ifdef _WIN32
#include "DiscSource.h"
#include <string>
#include <vector>
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <SDL3/SDL_properties.h>
#include <thread>
namespace {
// Convert a Windows filesystem name into the UTF-8 path expected by the application.
std::string utf8(const wchar_t* text) {
    const auto value = std::filesystem::path(text).u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
struct DiscEvents : IFileDialogEvents {
    std::vector<std::string>& paths;
    // Attach the dialog event handler to the caller's selected-path storage.
    explicit DiscEvents(std::vector<std::string>& p) : paths(p) {}
    // Expose the COM interfaces implemented by this dialog event handler.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IFileDialogEvents) { *out = this; AddRef(); return S_OK; }
        *out = nullptr; return E_NOINTERFACE;
    }
    // Keep the stack-owned event handler alive without transferring ownership to COM.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    // Acknowledge COM release without deleting the stack-owned event handler.
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // Accept an optical-disc folder as a source instead of navigating into it.
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog* dialog, IShellItem* folder) override {
        PWSTR name = nullptr;
        if (SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            auto path = std::filesystem::path(name); CoTaskMemFree(name);
            if (DiscSource::candidate(path)) {
                paths = {utf8(path.c_str())}; dialog->Close(S_OK); return S_FALSE;
            }
        }
        return S_OK;
    }
    // Allow the dialog's normal file acceptance behavior.
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override { return S_OK; }
    // Accept folder-change notifications without additional work.
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override { return S_OK; }
    // Accept selection-change notifications without additional work.
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { return S_OK; }
    // Leave sharing-violation handling to the Windows dialog's default behavior.
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE* r) override { *r = FDESVR_DEFAULT; return S_OK; }
    // Accept file-type changes without additional work.
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { return S_OK; }
    // Leave overwrite handling to the Windows dialog's default behavior.
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* r) override { *r = FDEOR_DEFAULT; return S_OK; }
};
}
// Run the Windows file dialog on its own COM thread with disc-folder selection support.
void MediaOpenDialog::open(SDL_DialogFileCallback callback, SDL_Window* parent,
                          const SDL_DialogFileFilter* filters, int count) {
    auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    std::vector<std::wstring> names, patterns;
    for (int i = 0; i < count; ++i) {
        names.emplace_back(std::filesystem::path(filters[i].name).wstring());
        std::string pattern = "*." + std::string(filters[i].pattern);
        for (size_t p = 0; (p = pattern.find(';', p)) != std::string::npos; p += 3) pattern.insert(p + 1, "*.");
        patterns.emplace_back(pattern.begin(), pattern.end());
    }
    std::thread([callback, hwnd, names = std::move(names), patterns = std::move(patterns)] {
        std::vector<std::string> paths;
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(initialized) && SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            std::vector<COMDLG_FILTERSPEC> specs;
            for (size_t i = 0; i < names.size(); ++i) specs.push_back({names[i].c_str(), patterns[i].c_str()});
            specs.push_back({L"All Files", L"*.*"});
            dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
            dialog->SetOptions(FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
            dialog->SetTitle(L"Load Media");
            DiscEvents events(paths); DWORD cookie = 0; dialog->Advise(&events, &cookie);
            if (SUCCEEDED(dialog->Show(hwnd)) && paths.empty()) {
                IShellItemArray* items = nullptr;
                if (SUCCEEDED(dialog->GetResults(&items))) {
                    DWORD count = 0; items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem* item = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, &item))) {
                            PWSTR name = nullptr;
                            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) { paths.push_back(utf8(name)); CoTaskMemFree(name); }
                            item->Release();
                        }
                    }
                    items->Release();
                }
            }
            dialog->Unadvise(cookie); dialog->Release();
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        std::vector<const char*> values;
        for (const auto& path : paths) values.push_back(path.c_str());
        values.push_back(nullptr); callback(nullptr, values.data(), 0);
    }).detach();
}
// Provide the common polling interface; the Windows dialog runs on its own thread.
void MediaOpenDialog::poll() {}
// Provide the common cleanup interface; the Windows dialog owns its lifetime.
void MediaOpenDialog::close() {}
#elif !defined(__APPLE__)
// Open the platform's standard SDL file chooser.
void MediaOpenDialog::open(SDL_DialogFileCallback callback, SDL_Window* parent,
                          const SDL_DialogFileFilter* filters, int count) {
    SDL_ShowOpenFileDialog(callback, nullptr, parent, filters, count, nullptr, true);
}
// Provide the common polling interface for the SDL-managed chooser.
void MediaOpenDialog::poll() {}
// Provide the common cleanup interface for the SDL-managed chooser.
void MediaOpenDialog::close() {}
#endif
