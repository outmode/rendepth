#include "MediaOpenDialog.h"
#include "DiscSource.h"
#include <SDL3/SDL_properties.h>
#import <Cocoa/Cocoa.h>
#include <string>
#include <vector>

namespace {
NSOpenPanel* panel = nil;

// Keep disc directories selectable. If Open is pressed on an ordinary directory,
// continue browsing there instead of returning it as media.
void showPanel(SDL_DialogFileCallback callback, NSWindow* window, NSURL* directory) {
    panel = [NSOpenPanel openPanel];
    panel.title = @"Load Media";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = YES;
    if (directory) panel.directoryURL = directory;
    auto completion = ^(NSModalResponse response) {
        std::vector<std::string> paths;
        if (response == NSModalResponseOK) {
            NSArray<NSURL*>* urls = panel.URLs;
            if (urls.count == 1) {
                NSURL* url = urls.firstObject;
                NSNumber* isDirectory = nil;
                [url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil];
                if (isDirectory.boolValue &&
                    !DiscSource::candidate(std::filesystem::path(url.fileSystemRepresentation))) {
                    panel = nil;
                    showPanel(callback, window, url);
                    return;
                }
            }
            for (NSURL* url in urls) paths.emplace_back(url.fileSystemRepresentation);
        }
        panel = nil;
        std::vector<const char*> values;
        for (const auto& path : paths) values.push_back(path.c_str());
        values.push_back(nullptr);
        callback(nullptr, values.data(), 0);
    };
    if (window) [panel beginSheetModalForWindow:window completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
}
}

void MediaOpenDialog::open(SDL_DialogFileCallback callback, SDL_Window* parent,
                          const SDL_DialogFileFilter*, int) {
    if (panel) return;
    auto* window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    showPanel(callback, window, nil);
}

void MediaOpenDialog::poll() {}
void MediaOpenDialog::close() { [panel cancel:nil]; }
