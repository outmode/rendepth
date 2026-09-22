#include "LicenseDialog.h"
#import <Cocoa/Cocoa.h>
namespace {
NSWindow* licenseWindow;
NSTextField *keyField, *statusField;
NSButton *activateButton, *deactivateButton;
Licensing::DialogState current;
Licensing::DialogAction callback;
// Convert UTF-8 application text to a Cocoa string.
NSString* ns(const std::string& text) { return [NSString stringWithUTF8String:text.c_str()]; }
}
@interface RendepthLicenseActions : NSObject <NSWindowDelegate>
- (void)activate:(id)sender;
- (void)deactivate:(id)sender;
- (void)buy:(id)sender;
- (void)support:(id)sender;
@end
@implementation RendepthLicenseActions
// Submit the entered key when activation is configured and no operation is pending.
- (void)activate:(id)sender {
    if (!current.busy && current.canActivate && callback) callback(false, [[keyField stringValue] UTF8String]);
}
// Confirm deactivation before sending it to the licensing service.
- (void)deactivate:(id)sender {
    if (current.busy || !callback) return;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Deactivate Rendepth Pro on this computer?";
    alert.informativeText = @"Internet access is required. Your activation will be kept if the request fails.";
    [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Deactivate"];
    if ([alert runModal] == NSAlertSecondButtonReturn) callback(true, {});
}
// Open the configured purchase page in the default browser.
- (void)buy:(id)sender { if (!current.purchaseUrl.empty()) SDL_OpenURL(current.purchaseUrl.c_str()); }
// Open the support link and display a fallback message if launching fails.
- (void)support:(id)sender {
    const auto& url = current.supportUrl;
    if (!url.empty() && !SDL_OpenURL(url.c_str())) {
        statusField.stringValue = ns(url.starts_with("mailto:") ?
            "Could not open your email app. Please email " + url.substr(7) : std::string("Could not open your browser."));
    }
}
// Hide the license window on close so it can be reused later.
- (BOOL)windowShouldClose:(NSWindow*)sender { [sender orderOut:nil]; return NO; }
@end
namespace Licensing::NativeDialog {
namespace { RendepthLicenseActions* actions; }
// Refresh Cocoa status and controls for the current activation and busy state.
void update(const DialogState& state) {
    current = state;
    if (!licenseWindow) return;
    statusField.stringValue = ns(state.message);
    keyField.hidden = state.licensed; activateButton.hidden = state.licensed; deactivateButton.hidden = !state.licensed;
    keyField.enabled = !state.busy && state.canActivate; activateButton.enabled = !state.busy && state.canActivate;
    deactivateButton.enabled = !state.busy;
    if (state.licensed) keyField.stringValue = @"";
}
// Create or focus the Cocoa license window and connect its controls to service actions.
void open(SDL_Window* parent, const DialogState& state, DialogAction action) {
    callback = std::move(action);
    if (licenseWindow) { update(state); [licenseWindow makeKeyAndOrderFront:nil]; return; }
    actions = [[RendepthLicenseActions alloc] init];
    licenseWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 380)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    licenseWindow.title = @"Rendepth Pro License"; licenseWindow.delegate = actions;
    licenseWindow.releasedWhenClosed = NO;
    auto label = [&](NSString* text, NSRect frame) {
        NSTextField* field = [NSTextField wrappingLabelWithString:text];
        field.frame = frame; [licenseWindow.contentView addSubview:field]; return field;
    };
    label(@"Activate once. Use forever.", NSMakeRect(24, 338, 492, 22));
    label(@"A perpetual license for up to 5 computers. After activation, no recurring license checks or internet connection are required for licensing.", NSMakeRect(24, 270, 492, 56));
    label(@"License Key", NSMakeRect(24, 242, 492, 22));
    keyField = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 206, 492, 28)];
    keyField.placeholderString = @"License Key"; keyField.target = actions; keyField.action = @selector(activate:);
    [licenseWindow.contentView addSubview:keyField];
    statusField = label(@"", NSMakeRect(24, 136, 492, 60));
    auto button = [&](NSString* title, NSRect frame, SEL action) {
        NSButton* b = [NSButton buttonWithTitle:title target:actions action:action];
        b.frame = frame; [licenseWindow.contentView addSubview:b]; return b;
    };
    activateButton = button(@"Activate Rendepth Pro", NSMakeRect(24, 96, 240, 32), @selector(activate:));
    deactivateButton = button(@"Deactivate This Computer", NSMakeRect(24, 96, 240, 32), @selector(deactivate:));
    auto* buy = button(@"Buy Rendepth Pro", NSMakeRect(24, 56, 180, 32), @selector(buy:));
    auto* support = button(@"Contact Support", NSMakeRect(220, 56, 180, 32), @selector(support:));
    buy.enabled = !state.purchaseUrl.empty(); support.enabled = !state.supportUrl.empty();
    label(@"Lost access to an old computer? Contact support to recover its activation slot.", NSMakeRect(24, 8, 492, 42));
    if (parent) {
        NSWindow* owner = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(parent),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (owner) [owner addChildWindow:licenseWindow ordered:NSWindowAbove];
    }
    update(state); [licenseWindow center]; [licenseWindow makeKeyAndOrderFront:nil];
}
void poll() {} // SDL services the Cocoa event loop on the main thread.
// Detach and release the license window and its action handler.
void close() {
    [licenseWindow.parentWindow removeChildWindow:licenseWindow];
    [licenseWindow close]; licenseWindow = nil; actions = nil; callback = {};
}
}
