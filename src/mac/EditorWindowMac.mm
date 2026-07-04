#import "EditorWindowMac.h"

#import "../PresetManager.h"
#import "Dialogs.h"

#import "pluginterfaces/base/funknownimpl.h"

#import <Cocoa/Cocoa.h>

#import <cstdio>
#import <string>

#if !__has_feature(objc_arc)
#error this file needs to be compiled with automatic reference counting enabled
#endif

using Steinberg::kInternalError;
using Steinberg::kInvalidArgument;
using Steinberg::kNoInterface;
using Steinberg::kPlatformTypeNSView;
using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::tresult;

//------------------------------------------------------------------------
// Forwards NSWindow delegate callbacks to the C++ EditorWindowMac. Holds a strong reference to keep
// the window object alive for the delegate's lifetime, mirroring the win32/linux `self` keep-alive.
@interface VSTDemonWindowDelegate : NSObject <NSWindowDelegate>
{
	std::shared_ptr<vstdemon::EditorWindowMac> _window;
}
- (instancetype)initWithWindow:(std::shared_ptr<vstdemon::EditorWindowMac>)window;
@end

@implementation VSTDemonWindowDelegate

- (instancetype)initWithWindow:(std::shared_ptr<vstdemon::EditorWindowMac>)window
{
	if (self = [super init])
		_window = window;
	return self;
}

- (NSSize)windowWillResize:(NSWindow*)sender toSize:(NSSize)frameSize
{
	NSRect r {};
	r.size = frameSize;
	r = [sender contentRectForFrameRect:r];
	double outWidth = r.size.width;
	double outHeight = r.size.height;
	_window->onWillResize (r.size.width, r.size.height, &outWidth, &outHeight);
	r.size.width = outWidth;
	r.size.height = outHeight;
	r = [sender frameRectForContentRect:r];
	return r.size;
}

- (void)windowDidResize:(NSNotification*)notification
{
	NSWindow* window = [notification object];
	NSRect r = [window contentRectForFrameRect:window.frame];
	_window->onDidResize (r.size.width, r.size.height);
}

- (void)windowWillClose:(NSNotification*)notification
{
	_window->onWindowWillClose ();
	_window = nullptr;
}

@end

namespace vstdemon {

namespace {

constexpr double kDirtyPollIntervalSeconds = 1.0;

EditorWindowMac* gActiveWindow {nullptr};

// The trailing path component (filename) of a UTF-8 path, for the title bar.
std::string fileNameOf (const std::string& path)
{
	auto pos = path.find_last_of ("/");
	return pos == std::string::npos ? path : path.substr (pos + 1);
}

} // namespace

//------------------------------------------------------------------------
void setActiveMacWindow (EditorWindowMac* window)
{
	gActiveWindow = window;
}

//------------------------------------------------------------------------
EditorWindowMac* activeMacWindow ()
{
	return gActiveWindow;
}

//------------------------------------------------------------------------
std::shared_ptr<EditorWindowMac> EditorWindowMac::make (const std::string& title,
                                                        const IPtr<IPlugView>& view,
                                                        PresetManager* presetManager, int closeAfterMs)
{
	auto window = std::make_shared<EditorWindowMac> ();
	window->presetManager = presetManager;
	window->className = title;
	window->closeAfterMs = closeAfterMs;
	if (window->init (title, view))
		return window;
	return nullptr;
}

//------------------------------------------------------------------------
EditorWindowMac::~EditorWindowMac () noexcept
{
	closePlugView ();
	if (nsWindow)
	{
		nsWindow.delegate = nil;
		[nsWindow close];
		nsWindow = nullptr;
	}
	nsWindowDelegate = nullptr;
}

//------------------------------------------------------------------------
bool EditorWindowMac::init (const std::string& title, const IPtr<IPlugView>& view)
{
	plugView = view;

	ViewRect plugViewSize {};
	if (plugView->getSize (&plugViewSize) != kResultTrue)
		return false;

	resizeable = plugView->canResize () == kResultTrue;
	const CGFloat width = static_cast<CGFloat> (plugViewSize.right - plugViewSize.left);
	const CGFloat height = static_cast<CGFloat> (plugViewSize.bottom - plugViewSize.top);

	NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	if (resizeable)
		styleMask |= NSWindowStyleMaskResizable;

	auto contentRect = NSMakeRect (0., 0., width, height);
	nsWindow = [[NSWindow alloc] initWithContentRect:contentRect
	                                       styleMask:styleMask
	                                         backing:NSBackingStoreBuffered
	                                           defer:YES];
	if (!nsWindow)
		return false;

	nsWindow.releasedWhenClosed = NO;
	nsWindow.title = [NSString stringWithUTF8String:title.c_str ()];
	[nsWindow center];
	return true;
}

//------------------------------------------------------------------------
bool EditorWindowMac::show ()
{
	if (!plugView || !nsWindow)
		return false;

	self = shared_from_this ();

	if (plugView->isPlatformTypeSupported (kPlatformTypeNSView) != kResultTrue)
	{
		std::fprintf (stderr, "Plugin editor does not support NSView embedding.\n");
		self = nullptr;
		return false;
	}

	nsWindowDelegate = [[VSTDemonWindowDelegate alloc] initWithWindow:self];
	nsWindow.delegate = nsWindowDelegate;

	plugView->setFrame (this);

	if (plugView->attached ((__bridge void*) [nsWindow contentView], kPlatformTypeNSView) != kResultTrue)
	{
		plugView->setFrame (nullptr);
		nsWindow.delegate = nil;
		nsWindowDelegate = nullptr;
		self = nullptr;
		return false;
	}
	viewAttached = true;

	dirtyPollTimer = [NSTimer scheduledTimerWithTimeInterval:kDirtyPollIntervalSeconds
	                                                 repeats:YES
	                                                   block:^(NSTimer*) { saveIfDirty (); }];
	timerActive = true;

	if (closeAfterMs > 0)
		closeAfterTimer = [NSTimer scheduledTimerWithTimeInterval:closeAfterMs / 1000.
		                                                  repeats:NO
		                                                    block:^(NSTimer*) { requestClose (); }];

	setActiveMacWindow (this);
	[nsWindow makeKeyAndOrderFront:nil];
	return true;
}

//------------------------------------------------------------------------
void EditorWindowMac::closePlugView ()
{
	if (plugView)
	{
		plugView->setFrame (nullptr);
		if (viewAttached)
		{
			plugView->removed ();
			viewAttached = false;
		}
		plugView = nullptr;
	}
}

//------------------------------------------------------------------------
void EditorWindowMac::onWindowWillClose ()
{
	if (timerActive)
	{
		[dirtyPollTimer invalidate];
		dirtyPollTimer = nullptr;
		timerActive = false;
	}
	if (closeAfterTimer)
	{
		[closeAfterTimer invalidate];
		closeAfterTimer = nullptr;
	}

	// Unconditional final save (design-cli: "final capture and write before exit"). Dormant (no
	// target) no-ops. Matches the Win32/Linux close behavior: save(), not saveIfDirty().
	if (presetManager && presetManager->hasTarget () && !presetManager->save ())
		std::fprintf (stderr, "Warning: failed to save preset on close; state was not persisted.\n");

	closePlugView ();
	setActiveMacWindow (nullptr);
	platform::quitEventLoop ();
	self = nullptr;
}

//------------------------------------------------------------------------
void EditorWindowMac::requestClose ()
{
	// Triggering the close through the NSWindow drives windowWillClose (→ onWindowWillClose), so the
	// menu ⌘W and the window's own close button share one teardown path.
	if (nsWindow)
		[nsWindow performClose:nil];
}

//------------------------------------------------------------------------
void EditorWindowMac::updateTitle ()
{
	if (!nsWindow)
		return;
	std::string title = className;
	if (presetManager && presetManager->hasTarget ())
		title += " — " + fileNameOf (presetManager->targetPath ());
	nsWindow.title = [NSString stringWithUTF8String:title.c_str ()];
}

//------------------------------------------------------------------------
void EditorWindowMac::postSaveRequest ()
{
	// A save requested from an edit callback (endEdit/restartComponent/setDirty). Defer it onto the
	// main queue so it lands on the same code path as the 1s dirty poll, off any reentrant plugin
	// context (mirrors the win32 posted save and the linux one-shot run-loop timer).
	auto keepAlive = self;
	dispatch_async (dispatch_get_main_queue (), ^{
		keepAlive->saveIfDirty ();
	});
}

//------------------------------------------------------------------------
void EditorWindowMac::saveIfDirty ()
{
	if (presetManager && !dialogOpen)
		presetManager->saveIfDirty ();
}

//------------------------------------------------------------------------
void EditorWindowMac::pauseDirtyPoll ()
{
	if (timerActive)
	{
		[dirtyPollTimer invalidate];
		dirtyPollTimer = nullptr;
		timerActive = false;
	}
}

//------------------------------------------------------------------------
void EditorWindowMac::resumeDirtyPoll ()
{
	if (!timerActive)
	{
		dirtyPollTimer = [NSTimer scheduledTimerWithTimeInterval:kDirtyPollIntervalSeconds
		                                                 repeats:YES
		                                                   block:^(NSTimer*) { saveIfDirty (); }];
		timerActive = true;
	}
}

//------------------------------------------------------------------------
void EditorWindowMac::onOpenPreset ()
{
	if (!presetManager)
		return;

	std::optional<std::string> path;
	{
		dialogOpen = true;
		pauseDirtyPoll ();
		path = runOpenPresetDialog (presetManager->hasTarget ()
		                                ? fileNameOf (presetManager->targetPath ())
		                                : className + ".vstpreset");
		resumeDirtyPoll ();
		dialogOpen = false;
	}
	if (!path)
		return;

	auto result = presetManager->openPreset (*path);
	if (!result.ok)
		std::fprintf (stderr, "%s\n", result.error.c_str ());
}

//------------------------------------------------------------------------
void EditorWindowMac::onSavePresetAs ()
{
	if (!presetManager)
		return;

	std::string suggested = presetManager->hasTarget ()
	                            ? fileNameOf (presetManager->targetPath ())
	                            : className + ".vstpreset";

	std::optional<std::string> path;
	{
		dialogOpen = true;
		pauseDirtyPoll ();
		path = runSavePresetDialog (suggested);
		resumeDirtyPoll ();
		dialogOpen = false;
	}
	if (!path)
		return;

	if (!presetManager->saveAs (*path))
		std::fprintf (stderr, "Warning: failed to save preset to '%s'.\n", path->c_str ());
}

//------------------------------------------------------------------------
void EditorWindowMac::onWillResize (double width, double height, double* outWidth, double* outHeight)
{
	if (!plugView)
		return;
	ViewRect r {};
	r.right = static_cast<Steinberg::int32> (width);
	r.bottom = static_cast<Steinberg::int32> (height);
	if (plugView->checkSizeConstraint (&r) == kResultTrue)
	{
		*outWidth = static_cast<double> (r.right - r.left);
		*outHeight = static_cast<double> (r.bottom - r.top);
	}
}

//------------------------------------------------------------------------
void EditorWindowMac::onDidResize (double width, double height)
{
	if (!plugView)
		return;
	// Tell the plugin the final content size unconditionally (matching the win32/linux legs — onSize is
	// not gated on the constraint query, or a plugin returning kNotImplemented would never relayout).
	ViewRect r {};
	r.right = static_cast<Steinberg::int32> (width);
	r.bottom = static_cast<Steinberg::int32> (height);
	plugView->onSize (&r);
}

//------------------------------------------------------------------------
tresult PLUGIN_API EditorWindowMac::resizeView (IPlugView* view, ViewRect* newSize)
{
	if (newSize == nullptr || view == nullptr || view != plugView)
		return kInvalidArgument;
	if (!nsWindow)
		return kInternalError;
	if (resizeViewRecursionGuard)
		return kResultFalse;

	ViewRect current {};
	if (plugView->getSize (&current) != kResultTrue)
		return kInternalError;
	if (current.left == newSize->left && current.top == newSize->top &&
	    current.right == newSize->right && current.bottom == newSize->bottom)
		return kResultTrue;

	const CGFloat width = static_cast<CGFloat> (newSize->right - newSize->left);
	const CGFloat height = static_cast<CGFloat> (newSize->bottom - newSize->top);

	// Plugin-driven resize: keep the window's top-left anchored (Cocoa origin is bottom-left, so grow
	// downward by shifting origin.y). Mirrors the sample's Window::resize.
	resizeViewRecursionGuard = true;
	NSRect frameContent = [nsWindow contentRectForFrameRect:nsWindow.frame];
	CGFloat diff = height - frameContent.size.height;
	frameContent.size.width = width;
	frameContent.size.height = height;
	frameContent.origin.y -= diff;
	[nsWindow setFrame:[nsWindow frameRectForContentRect:frameContent]
	           display:nsWindow.isVisible
	           animate:NO];
	resizeViewRecursionGuard = false;

	if (plugView->onSize (newSize) != kResultTrue)
		return kInternalError;
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EditorWindowMac::queryInterface (const TUID iid, void** obj)
{
	if (!obj)
		return kInvalidArgument;
	if (Steinberg::FUnknownPrivate::iidEqual (iid, IPlugFrame::iid) ||
	    Steinberg::FUnknownPrivate::iidEqual (iid, FUnknown::iid))
	{
		*obj = static_cast<IPlugFrame*> (this);
		addRef ();
		return kResultTrue;
	}
	*obj = nullptr;
	return kNoInterface;
}

//------------------------------------------------------------------------
std::shared_ptr<PlatformWindow> makePlatformWindow (const std::string& title,
                                                    const IPtr<IPlugView>& view,
                                                    PresetManager* presetManager, int closeAfterMs)
{
	return EditorWindowMac::make (title, view, presetManager, closeAfterMs);
}

} // namespace vstdemon
