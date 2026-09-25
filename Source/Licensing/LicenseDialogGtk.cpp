#include "LicenseDialog.h"
#include <gtk/gtk.h>
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif
namespace Licensing::NativeDialog {
namespace {
GtkWidget *window = nullptr, *entry = nullptr, *status = nullptr, *activate = nullptr, *deactivate = nullptr;
DialogState current;
DialogAction callback;
// Open a purchase or support link and show an actionable error if launching fails.
void launch(const std::string& url) {
    if (!url.empty() && !SDL_OpenURL(url.c_str())) {
        gtk_label_set_text(GTK_LABEL(status), "Could not open your browser.");
    }
}
// Confirm deactivation with the user before dispatching the license operation.
void deactivateClicked() {
    auto* confirm = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
        "Deactivate Rendepth Pro on this computer?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm),
        "Internet access is required. Your activation will be kept if the request fails.");
    const auto answer = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (answer == GTK_RESPONSE_OK && callback) callback(true, {});
}
}
// Refresh license status and control visibility, disabling actions while work is pending.
void update(const DialogState& state) {
    current = state;
    if (!window) return;
    gtk_label_set_text(GTK_LABEL(status), state.message.c_str());
    gtk_widget_set_visible(entry, !state.licensed);
    gtk_widget_set_visible(activate, !state.licensed);
    gtk_widget_set_visible(deactivate, state.licensed);
    gtk_widget_set_sensitive(entry, !state.busy && state.canActivate);
    gtk_widget_set_sensitive(activate, !state.busy && state.canActivate);
    gtk_widget_set_sensitive(deactivate, !state.busy);
    if (state.licensed) gtk_entry_set_text(GTK_ENTRY(entry), "");
}
// Create or focus the GTK license window and connect its actions to the service callback.
void open(SDL_Window* parent, const DialogState& state, DialogAction action) {
    callback = std::move(action);
    if (window) { update(state); gtk_window_present(GTK_WINDOW(window)); return; }
    if (!gtk_init_check(nullptr, nullptr)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rendepth Pro", "Could not open the native license window.", parent); return;
    }
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Rendepth Pro License");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 360);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 24);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_add(GTK_CONTAINER(window), box);
    auto addLabel = [&](const char* text) {
        auto* label = gtk_label_new(text);
        gtk_label_set_line_wrap(GTK_LABEL(label), true);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_box_pack_start(GTK_BOX(box), label, false, false, 0);
        return label;
    };
    addLabel("Activate once. Use forever.");
    addLabel("A perpetual license for up to 5 computers. After activation, no recurring license checks or internet connection are required for licensing.");
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "License Key");
    atk_object_set_name(gtk_widget_get_accessible(entry), "License Key");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 256);
    gtk_box_pack_start(GTK_BOX(box), entry, false, false, 0);
    status = addLabel("");
    auto* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), buttons, false, false, 0);
    auto button = [&](const char* label) {
        auto* b = gtk_button_new_with_label(label); gtk_container_add(GTK_CONTAINER(buttons), b); return b;
    };
    activate = button("Activate Rendepth Pro"); deactivate = button("Deactivate This Computer");
    g_signal_connect(activate, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        if (callback && !current.busy) callback(false, gtk_entry_get_text(GTK_ENTRY(entry)));
    }), nullptr);
    g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer) {
        if (callback && !current.busy && current.canActivate) callback(false, gtk_entry_get_text(GTK_ENTRY(entry)));
    }), nullptr);
    g_signal_connect(deactivate, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { deactivateClicked(); }), nullptr);
    auto* links = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), links, false, false, 0);
    auto* buy = gtk_button_new_with_label("Buy Rendepth Pro"); gtk_container_add(GTK_CONTAINER(links), buy);
    gtk_widget_set_sensitive(buy, !state.purchaseUrl.empty());
    g_signal_connect(buy, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { launch(current.purchaseUrl); }), nullptr);
    auto* support = gtk_button_new_with_label("Join Community"); gtk_container_add(GTK_CONTAINER(links), support);
    gtk_widget_set_sensitive(support, !state.supportUrl.empty());
    g_signal_connect(support, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { launch(current.supportUrl); }), nullptr);
    addLabel("Lost access to an old computer? Contact support to recover its activation slot.");
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        window = entry = status = activate = deactivate = nullptr;
    }), nullptr);
    gtk_widget_show_all(window);
#if defined(GDK_WINDOWING_X11)
    if (parent && GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        const auto xid = SDL_GetNumberProperty(SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (xid) {
            auto* owner = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(), static_cast<Window>(xid));
            if (owner) { gdk_window_set_transient_for(gtk_widget_get_window(window), owner); g_object_unref(owner); }
        }
    }
#endif
    update(state);
    if (!state.licensed) gtk_widget_grab_focus(entry);
}
// Pump a bounded number of GTK events without blocking the SDL main loop.
void poll() { if (window) for (int i = 0; i < 32 && gtk_events_pending(); ++i) gtk_main_iteration_do(false); }
// Destroy the license window and release its action callback.
void close() { if (window) gtk_widget_destroy(window); callback = {}; }
}
