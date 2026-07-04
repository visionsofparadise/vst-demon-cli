#import "../Platform.h"
#import "EditorWindowMac.h"

#import <Cocoa/Cocoa.h>

#if !__has_feature(objc_arc)
#error this file needs to be compiled with automatic reference counting enabled
#endif

//------------------------------------------------------------------------
// Target for the File-menu items. Routes Open Preset / Save Preset As / Close to the active editor
// window (there is only ever one per process). Kept alive for the process lifetime by the app menu.
@interface VSTDemonMenuTarget : NSObject
- (void)openPreset:(id)sender;
- (void)savePresetAs:(id)sender;
- (void)closeWindow:(id)sender;
- (void)quit:(id)sender;
@end

@implementation VSTDemonMenuTarget

- (void)openPreset:(id)sender
{
	if (auto* window = vstdemon::activeMacWindow ())
		window->onOpenPreset ();
}

- (void)savePresetAs:(id)sender
{
	if (auto* window = vstdemon::activeMacWindow ())
		window->onSavePresetAs ();
}

- (void)closeWindow:(id)sender
{
	if (auto* window = vstdemon::activeMacWindow ())
		window->requestClose ();
}

- (void)quit:(id)sender
{
	// Quit routes through the window's close teardown (final save + closed event + clean exit), NOT
	// -[NSApplication terminate:], which exit()s without sending windowWillClose — that would skip the
	// final save and the closed event, breaking the auto-save-on-close and stdout contracts.
	if (auto* window = vstdemon::activeMacWindow ())
		window->requestClose ();
	else
		vstdemon::platform::quitEventLoop ();
}

@end

namespace vstdemon {
namespace platform {

namespace {

VSTDemonMenuTarget* gMenuTarget {nullptr};

// Build the NSMenu menubar: an app menu (Hide/Quit) and a File menu (Open Preset ⌘O, Save Preset As
// ⇧⌘S, Close ⌘W). Written fresh for a plain console binary — no bundle, no CFBundleName lookup.
void buildMenuBar ()
{
	gMenuTarget = [VSTDemonMenuTarget new];

	NSMenu* mainMenu = [NSMenu new];
	[NSApp setMainMenu:mainMenu];

	NSString* appName = @"vst-demon-cli";

	NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:appName action:nil keyEquivalent:@""];
	[mainMenu addItem:appMenuItem];
	NSMenu* appMenu = [[NSMenu alloc] initWithTitle:appName];
	[appMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
	                   action:@selector (hide:)
	            keyEquivalent:@"h"];
	[appMenu addItem:[NSMenuItem separatorItem]];
	NSMenuItem* quitItem = [appMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
	                                          action:@selector (quit:)
	                                   keyEquivalent:@"q"];
	quitItem.target = gMenuTarget;
	appMenuItem.submenu = appMenu;

	NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
	[mainMenu addItem:fileMenuItem];
	NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
	NSMenuItem* openItem = [fileMenu addItemWithTitle:@"Open Preset…"
	                                           action:@selector (openPreset:)
	                                    keyEquivalent:@"o"];
	openItem.target = gMenuTarget;
	NSMenuItem* saveAsItem = [fileMenu addItemWithTitle:@"Save Preset As…"
	                                             action:@selector (savePresetAs:)
	                                      keyEquivalent:@"S"];
	saveAsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
	saveAsItem.target = gMenuTarget;
	[fileMenu addItem:[NSMenuItem separatorItem]];
	NSMenuItem* closeItem = [fileMenu addItemWithTitle:@"Close"
	                                            action:@selector (closeWindow:)
	                                     keyEquivalent:@"w"];
	closeItem.target = gMenuTarget;
	fileMenuItem.submenu = fileMenu;
}

} // namespace

//------------------------------------------------------------------------
void initialize ()
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		buildMenuBar ();
		[NSApp activateIgnoringOtherApps:YES];
	}
}

//------------------------------------------------------------------------
void terminate ()
{
	gMenuTarget = nullptr;
}

//------------------------------------------------------------------------
void runEventLoop ()
{
	[NSApp run];
}

//------------------------------------------------------------------------
void quitEventLoop ()
{
	// Ask AppKit to stop the run loop, then post a dummy event so -[NSApp run] wakes and returns
	// promptly rather than waiting for the next real event (the standard AppKit stop idiom).
	[NSApp stop:nil];
	@autoreleasepool
	{
		NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
		                                    location:NSMakePoint (0, 0)
		                               modifierFlags:0
		                                   timestamp:0
		                                windowNumber:0
		                                     context:nil
		                                     subtype:0
		                                       data1:0
		                                       data2:0];
		[NSApp postEvent:event atStart:YES];
	}
}

//------------------------------------------------------------------------
Steinberg::FUnknown* getPluginFactoryContext ()
{
	return nullptr;
}

} // namespace platform
} // namespace vstdemon
