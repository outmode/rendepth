#include "MediaOpenDialog.h"
#include "DiscSource.h"
#include <SDL3/SDL_properties.h>
#import <Cocoa/Cocoa.h>
#include <string>
#include <vector>

// Permit selecting an optical volume or disc folder without accepting ordinary
// folders as media. The panel still allows navigating through those folders.
@interface RendepthDiscPanelDelegate : NSObject <NSOpenSavePanelDelegate>
@end
@implementation RendepthDiscPanelDelegate
- (BOOL)panel:(id)sender validateURL:(NSURL*)url error:(NSError**)error {
    NSNumber* directory = nil;
    [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
    return !directory.boolValue || DiscSource::candidate(std::filesystem::path(url.fileSystemRepresentation));
}
@end

namespace {
NSOpenPanel* panel = nil;
RendepthDiscPanelDelegate* delegate = nil;
}

void MediaOpenDialog::open(SDL_DialogFileCallback callback, SDL_Window* parent,
                          const SDL_DialogFileFilter*, int) {
    if (panel) return;
    panel = [NSOpenPanel openPanel];
    delegate = [RendepthDiscPanelDelegate new];
    panel.delegate = delegate;
    panel.title = @"Load Media";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = YES;
    auto completion = ^(NSModalResponse response) {
        std::vector<std::string> paths;
        if (response == NSModalResponseOK)
            for (NSURL* url in panel.URLs) paths.emplace_back(url.fileSystemRepresentation);
        panel = nil;
        delegate = nil;
        std::vector<const char*> values;
        for (const auto& path : paths) values.push_back(path.c_str());
        values.push_back(nullptr);
        callback(nullptr, values.data(), 0);
    };
    auto* window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (window) [panel beginSheetModalForWindow:window completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
}

void MediaOpenDialog::poll() {}
void MediaOpenDialog::close() { [panel cancel:nil]; }
