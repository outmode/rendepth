#include "LicenseDialog.h"
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL_system.h>
#include <vector>
namespace Licensing::NativeDialog {
namespace {
HWND window = nullptr, entry = nullptr, status = nullptr, activate = nullptr, deactivate = nullptr;
DialogState current;
DialogAction callback;
HFONT font = nullptr;
enum { Key = 100, Activate, Deactivate, Buy, Support, Close };
// Convert UTF-8 application text to the wide strings expected by Win32 controls.
std::wstring wide(const std::string& value) {
    int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), n); return out;
}
// Read a Windows control's text as UTF-8 for the licensing service.
std::string text(HWND control) {
    std::wstring value(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), n, nullptr, nullptr);
    if (!out.empty()) out.pop_back(); return out;
}
// Dispatch license-window commands, confirmation prompts, and cleanup messages.
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(w);
        if (id == Close || id == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        if ((id == Activate || id == IDOK) && !current.busy && !current.licensed && current.canActivate && callback)
            callback(false, text(entry));
        if (id == Deactivate && !current.busy && current.licensed && callback &&
            MessageBoxW(hwnd, L"Deactivate Rendepth Pro on this computer?\n\nInternet access is required. Your activation will be kept if the request fails.",
                L"Deactivate This Computer", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) callback(true, {});
        if (id == Buy || id == Support) {
            const auto& url = id == Buy ? current.purchaseUrl : current.supportUrl;
            if (!url.empty() && !SDL_OpenURL(url.c_str())) {
                const auto error = url.starts_with("mailto:") ? "Could not open your email app. Please email " + url.substr(7) :
                    std::string("Could not open your browser.");
                SetWindowTextW(status, wide(error).c_str());
            }
        }
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        window = entry = status = activate = deactivate = nullptr;
        if (font) { DeleteObject(font); font = nullptr; }
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}
// Update status text and enable only the actions valid for the current licensing state.
void update(const DialogState& state) {
    current = state;
    if (!window) return;
    SetWindowTextW(status, wide(state.message).c_str());
    ShowWindow(entry, state.licensed ? SW_HIDE : SW_SHOW);
    ShowWindow(activate, state.licensed ? SW_HIDE : SW_SHOW);
    ShowWindow(deactivate, state.licensed ? SW_SHOW : SW_HIDE);
    EnableWindow(entry, !state.busy && state.canActivate);
    EnableWindow(activate, !state.busy && state.canActivate);
    EnableWindow(deactivate, !state.busy);
    if (state.licensed) SetWindowTextW(entry, L"");
}
// Create or focus a DPI-scaled Windows license dialog and integrate its keyboard handling with SDL.
void open(SDL_Window* parent, const DialogState& state, DialogAction action) {
    callback = std::move(action);
    if (window) { update(state); ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); return; }
    WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"RendepthLicenseDialog"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&cls);
    auto owner = parent ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) : nullptr;
    const UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    const auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
    RECT bounds{0, 0, scale(540), scale(388)};
    AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls.lpszClassName,
        L"Rendepth Pro License", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, cls.hInstance, nullptr);
    if (!window) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rendepth Pro", "Could not open the native license window.", parent); return; }
    font = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto control = [&](const wchar_t* type, const wchar_t* label, int id, int x, int y, int width, int height, DWORD style) {
        HWND c = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, label,
            WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return c;
    };
    control(L"STATIC", L"Activate once. Use forever.", 0, 24, 20, 490, 24, 0);
    control(L"STATIC", L"A perpetual license for up to 5 computers. After activation, no recurring license checks or internet connection are required for licensing.", 0, 24, 54, 490, 50, 0);
    control(L"STATIC", L"License Key", 0, 24, 112, 490, 20, 0);
    entry = control(L"EDIT", L"", Key, 24, 138, 490, 28, WS_TABSTOP | ES_AUTOHSCROLL);
    SendMessageW(entry, EM_SETLIMITTEXT, 256, 0);
    status = control(L"STATIC", L"", 0, 24, 180, 490, 66, 0);
    activate = control(L"BUTTON", L"Activate Rendepth Pro", Activate, 24, 250, 240, 32, WS_TABSTOP | BS_DEFPUSHBUTTON);
    deactivate = control(L"BUTTON", L"Deactivate This Computer", Deactivate, 24, 250, 240, 32, WS_TABSTOP);
    auto buy = control(L"BUTTON", L"Buy Rendepth Pro", Buy, 24, 294, 160, 30, WS_TABSTOP);
    auto support = control(L"BUTTON", L"Contact Support", Support, 196, 294, 160, 30, WS_TABSTOP);
    control(L"BUTTON", L"Close", Close, 368, 294, 146, 30, WS_TABSTOP);
    EnableWindow(buy, !state.purchaseUrl.empty()); EnableWindow(support, !state.supportUrl.empty());
    control(L"STATIC", L"Lost access to an old computer? Contact support to recover its activation slot.", 0, 24, 338, 490, 40, 0);
    SDL_SetWindowsMessageHook(+[](void*, MSG* message) -> bool {
        return !window || !IsDialogMessageW(window, message);
    }, nullptr);
    update(state); ShowWindow(window, SW_SHOW); SetForegroundWindow(window); SetFocus(entry);
}
// Pump a bounded number of license-dialog messages through Windows dialog handling.
void poll() {
    MSG message;
    for (int i = 0; window && i < 32 && PeekMessageW(&message, window, 0, 0, PM_REMOVE); ++i) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
// Destroy the license window and clear its callback.
void close() { if (window) DestroyWindow(window); callback = {}; }
}
