#include "LicenseDialog.h"
#include <gtk/gtk.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace Licensing;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static GtkWidget* find(GtkWidget* widget, const char* label = nullptr) {
    if ((!label && GTK_IS_ENTRY(widget)) || (label && GTK_IS_BUTTON(widget) &&
        std::string_view(gtk_button_get_label(GTK_BUTTON(widget))) == label)) return widget;
    if (!GTK_IS_CONTAINER(widget)) return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget* result = nullptr;
    for (auto* child = children; child && !result; child = child->next) result = find(GTK_WIDGET(child->data), label);
    g_list_free(children); return result;
}
static GtkWidget* window() {
    auto* windows = gtk_window_list_toplevels(); GtkWidget* result = nullptr;
    for (auto* it = windows; it; it = it->next) {
        auto* title = gtk_window_get_title(GTK_WINDOW(it->data));
        if (title && std::string_view(title) == "Rendepth Pro License") result = GTK_WIDGET(it->data);
    }
    g_list_free(windows); return result;
}
int main() {
    try {
        require(gtk_init_check(nullptr, nullptr), "GTK display unavailable");
        bool called = false, deactivated = false; std::string received;
        auto action = [&](bool deactivate, std::string key) { called = true; deactivated = deactivate; received = key; };
        DialogState state{false, false, true, "Enter the license key from your purchase email.", "https://example.com/buy", "https://example.com/support"};
        NativeDialog::open(nullptr, state, action); NativeDialog::poll();
        auto* w = window(); require(w, "native window exists");
        auto* entry = find(w); auto* activate = find(w, "Activate Rendepth Pro");
        require(entry && activate && gtk_widget_get_sensitive(activate), "activation controls");
        gtk_entry_set_text(GTK_ENTRY(entry), "test-key"); gtk_button_clicked(GTK_BUTTON(activate));
        require(called && !deactivated && received == "test-key", "native entry sends key");
        state.busy = true; NativeDialog::update(state);
        require(!gtk_widget_get_sensitive(activate) && !gtk_widget_get_sensitive(entry), "busy controls prevent duplicate requests");
        state.busy = false; state.licensed = true; state.message = "Rendepth Pro is activated on this computer.";
        NativeDialog::update(state);
        auto* deactivate = find(w, "Deactivate This Computer");
        require(!gtk_widget_get_visible(entry) && gtk_widget_get_visible(deactivate), "licensed controls");
        require(std::string_view(gtk_entry_get_text(GTK_ENTRY(entry))).empty(), "key cleared after activation");
        g_idle_add(+[](gpointer) -> gboolean {
            auto* windows = gtk_window_list_toplevels();
            for (auto* it = windows; it; it = it->next)
                if (GTK_IS_MESSAGE_DIALOG(it->data)) gtk_dialog_response(GTK_DIALOG(it->data), GTK_RESPONSE_OK);
            g_list_free(windows); return G_SOURCE_REMOVE;
        }, nullptr);
        gtk_button_clicked(GTK_BUTTON(deactivate)); require(deactivated, "confirmed deactivation callback");
        NativeDialog::close(); require(!window(), "close native window");
        state.licensed = false; state.canActivate = false; state.message = "Pro activation is not available in this build yet.";
        NativeDialog::open(nullptr, state, action); NativeDialog::poll();
        require(!gtk_widget_get_sensitive(find(window(), "Activate Rendepth Pro")), "unconfigured activation disabled");
        NativeDialog::close();
        std::cout << "PASS: native license dialog, entry, busy state, activation status, deactivation confirmation and reopening\n";
    } catch (const std::exception& e) { NativeDialog::close(); std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
