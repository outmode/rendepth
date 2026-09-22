#include "MediaOpenDialog.h"
#include "DiscSource.h"
#include <string>
#include <vector>
#ifdef RENDEPTH_GTK_MEDIA_DIALOG
#include <gtk/gtk.h>
namespace {
GtkWidget* dialog = nullptr;
GtkFileChooser* chooser = nullptr;
SDL_DialogFileCallback resultCallback = nullptr;
bool folderChanged = false;
// Convert a chooser URI to a local path when one is available.
std::string localPath(const char* uri) {
    if (!uri) return {};
    GFile* file = g_file_new_for_uri(uri);
    char* path = g_file_get_path(file);
    std::string value = path ? path : uri;
    g_free(path); g_object_unref(file);
    return value;
}
// Close the chooser and deliver its selected paths through the SDL-style callback.
void finish(const std::vector<std::string>& paths) {
    auto callback = resultCallback;
    MediaOpenDialog::close();
    std::vector<const char*> names;
    for (const auto& path : paths) names.push_back(path.c_str());
    names.push_back(nullptr);
    if (callback) callback(nullptr, names.data(), 0);
}
// Accept files or a disc source, while navigating into ordinary directories.
void accept() {
    std::vector<std::string> paths;
    auto* uris = gtk_file_chooser_get_uris(chooser);
    for (auto* entry = uris; entry; entry = entry->next)
        paths.push_back(localPath(static_cast<const char*>(entry->data)));
    g_slist_free_full(uris, g_free);
    if (paths.empty()) {
        char* uri = gtk_file_chooser_get_current_folder_uri(chooser);
        auto path = localPath(uri); g_free(uri);
        if (DiscSource::candidate(path)) paths.push_back(path);
    }
    if (paths.size() == 1) {
        std::error_code ec;
        if (std::filesystem::is_directory(paths[0], ec) && !DiscSource::candidate(paths[0])) {
            gtk_file_chooser_set_current_folder(chooser, paths[0].c_str());
            return;
        }
    }
    if (!paths.empty()) finish(paths);
}
}
// Open a GTK media chooser that can accept disc directories, falling back to SDL when GTK is
// unavailable.
void MediaOpenDialog::open(SDL_DialogFileCallback callback, SDL_Window* parent,
                          const SDL_DialogFileFilter* filters, int count) {
    if (dialog) { gtk_window_present(GTK_WINDOW(dialog)); return; }
    if (!gtk_init_check(nullptr, nullptr)) {
        SDL_ShowOpenFileDialog(callback, nullptr, parent, filters, count, nullptr, true);
        return;
    }
    resultCallback = callback;
    // A regular dialog lets Open accept a disc directory. GtkFileChooserDialog's
    // built-in Open response always navigates into directories instead.
    dialog = gtk_dialog_new_with_buttons("Load Media", nullptr, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 960, 640);
    auto* widget = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
    chooser = GTK_FILE_CHOOSER(widget);
    gtk_file_chooser_set_local_only(chooser, false);
    gtk_file_chooser_set_select_multiple(chooser, true);
    for (int i = 0; i < count; ++i) {
        auto* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, filters[i].name);
        std::string patterns = filters[i].pattern;
        size_t start = 0;
        do {
            auto end = patterns.find(';', start);
            auto pattern = "*." + patterns.substr(start, end - start);
            gtk_file_filter_add_pattern(filter, pattern.c_str());
            if (end == std::string::npos) break;
            start = end + 1;
        } while (true);
        gtk_file_chooser_add_filter(chooser, filter);
    }
    auto* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All Files"); gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(chooser, all);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), widget);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog*, gint response, gpointer) {
        if (response == GTK_RESPONSE_ACCEPT) accept(); else finish({});
    }), nullptr);
    g_signal_connect(widget, "file-activated", G_CALLBACK(+[](GtkFileChooser*, gpointer) { accept(); }), nullptr);
    g_signal_connect(widget, "current-folder-changed", G_CALLBACK(+[](GtkFileChooser*, gpointer) { folderChanged = true; }), nullptr);
    gtk_file_chooser_set_current_folder(chooser, g_get_home_dir());
    gtk_widget_show_all(dialog);
}
// Pump chooser events on the main thread and recognize disc selections from sidebar navigation.
void MediaOpenDialog::poll() {
    // Service the chooser on the SDL main thread; playback keeps running.
    if (!dialog) return;
    for (int i = 0; i < 32 && gtk_events_pending(); ++i) gtk_main_iteration_do(false);
    if (dialog && folderChanged) {
        folderChanged = false;
        char* uri = gtk_file_chooser_get_current_folder_uri(chooser);
        auto path = localPath(uri); g_free(uri);
        // Selecting a mounted drive in the sidebar opens its disc contents.
        if (!path.empty() && DiscSource::candidate(path)) finish({path});
    }
}
// Destroy the GTK chooser and clear its callback state.
void MediaOpenDialog::close() {
    if (dialog) gtk_widget_destroy(dialog);
    dialog = nullptr; chooser = nullptr; resultCallback = nullptr; folderChanged = false;
}
#elif defined(_WIN32)
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
#else
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
