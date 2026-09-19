#include "MediaOpenDialog.h"
#include <gtk/gtk.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include <chrono>
static bool called = false;
static std::vector<std::string> paths;
static void SDLCALL callback(void*, const char* const* files, int) {
    called = true; paths.clear();
    for (; files && *files; ++files) paths.emplace_back(*files);
}
static GtkWidget* findChooser(GtkWidget* w) {
    if (GTK_IS_FILE_CHOOSER(w)) return w;
    if (!GTK_IS_CONTAINER(w)) return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(w));
    GtkWidget* found = nullptr;
    for (auto* c = children; c && !found; c = c->next) found = findChooser(GTK_WIDGET(c->data));
    g_list_free(children); return found;
}
static void pump(int ticks = 100) {
    for (int i = 0; i < ticks; ++i) { MediaOpenDialog::poll(); g_usleep(10000); }
}
int main() {
    assert(gtk_init_check(nullptr, nullptr));
    const auto root = std::filesystem::temp_directory_path() / ("rendepth-dialog-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "ordinary");
    std::filesystem::create_directories(root / "disc/BDMV");
    std::ofstream(root / "disc/BDMV/index.bdmv").put('\0');
    std::ofstream(root / "ordinary/picture.png").put('\0');
    const SDL_DialogFileFilter filter{"Images", "png"};
    auto open = [&] {
        called = false; MediaOpenDialog::open(callback, nullptr, &filter, 1); pump();
        auto* tops = gtk_window_list_toplevels(); GtkWidget* chooser = nullptr;
        for (auto* t = tops; t && !chooser; t = t->next) chooser = findChooser(GTK_WIDGET(t->data));
        g_list_free(tops); assert(chooser); return GTK_FILE_CHOOSER(chooser);
    };
    auto* chooser = open();
    gtk_file_chooser_set_current_folder(chooser, (root / "ordinary").c_str()); pump();
    assert(!called);
    gtk_file_chooser_select_filename(chooser, (root / "ordinary/picture.png").c_str()); pump();
    gtk_dialog_response(GTK_DIALOG(gtk_widget_get_toplevel(GTK_WIDGET(chooser))), GTK_RESPONSE_ACCEPT); pump();
    assert(called && paths == std::vector<std::string>{(root / "ordinary/picture.png").string()});
    chooser = open();
    gtk_file_chooser_set_current_folder(chooser, (root / "disc").c_str()); pump();
    assert(called && paths == std::vector<std::string>{(root / "disc").string()});
    chooser = open();
    gtk_dialog_response(GTK_DIALOG(gtk_widget_get_toplevel(GTK_WIDGET(chooser))), GTK_RESPONSE_CANCEL); pump();
    assert(called && paths.empty());
    std::filesystem::remove_all(root);
    std::puts("Media dialog: normal folder navigation, file selection, disc root selection and cancellation passed");
}
