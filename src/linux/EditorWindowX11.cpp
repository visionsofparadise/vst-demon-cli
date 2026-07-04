#include "EditorWindowX11.h"

#include "../PresetManager.h"
#include "Dialogs.h"
#include "IRunLoopImpl.h"

#include "pluginterfaces/base/funknownimpl.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <X11/Xatom.h>
#include <X11/Xutil.h>

using Steinberg::kInternalError;
using Steinberg::kInvalidArgument;
using Steinberg::kNoInterface;
using Steinberg::kPlatformTypeX11EmbedWindowID;
using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::tresult;

namespace vstdemon {

namespace {

constexpr TimerIntervalMs kDirtyPollIntervalMs = 1000;

// XEMBED protocol opcodes (from the XEMBED spec; the SDK sample carries the same set).
constexpr long XEMBED_EMBEDDED_NOTIFY = 0;
constexpr long XEMBED_WINDOW_ACTIVATE = 1;
constexpr long XEMBED_WINDOW_DEACTIVATE = 2;
constexpr long XEMBED_REQUEST_FOCUS = 3;
constexpr long XEMBED_FOCUS_IN = 4;
constexpr uint32_t XEMBED_MAPPED = (1 << 0);

// The trailing path component (filename) of a UTF-8 path, for the title bar.
std::string fileNameOf (const std::string& path)
{
	auto pos = path.find_last_of ("/");
	return pos == std::string::npos ? path : path.substr (pos + 1);
}

void sendXEmbedMessage (Display* dpy, Window w, Atom messageType, long message, long detail,
                        long data1, long data2)
{
	XEvent ev;
	std::memset (&ev, 0, sizeof (ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = messageType;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = message;
	ev.xclient.data.l[2] = detail;
	ev.xclient.data.l[3] = data1;
	ev.xclient.data.l[4] = data2;
	XSendEvent (dpy, w, False, NoEventMask, &ev);
	XSync (dpy, False);
}

} // namespace

//------------------------------------------------------------------------
std::shared_ptr<EditorWindowX11> EditorWindowX11::make (const std::string& title,
                                                        const IPtr<IPlugView>& view,
                                                        PresetManager* presetManager)
{
	auto window = std::make_shared<EditorWindowX11> ();
	window->presetManager = presetManager;
	window->className = title;
	if (window->init (title, view))
		return window;
	return nullptr;
}

//------------------------------------------------------------------------
EditorWindowX11::~EditorWindowX11 () noexcept
{
	closePlugView ();
	if (xDisplay && xWindow)
	{
		RunLoop::instance ().unregisterWindow (xWindow);
		RunLoop::instance ().unregisterWindow (plugParentWindow);
		if (plugWindow)
			RunLoop::instance ().unregisterWindow (plugWindow);
		XDestroyWindow (xDisplay, xWindow);
		xWindow = 0;
	}
}

//------------------------------------------------------------------------
bool EditorWindowX11::init (const std::string& title, const IPtr<IPlugView>& view)
{
	plugView = view;
	xDisplay = RunLoop::instance ().getDisplay ();
	if (!xDisplay)
	{
		std::fprintf (stderr, "No X display for the editor window.\n");
		return false;
	}

	ViewRect plugViewSize {};
	if (plugView->getSize (&plugViewSize) != kResultTrue)
		return false;

	resizeable = plugView->canResize () == kResultTrue;
	currentWidth = plugViewSize.right - plugViewSize.left;
	currentHeight = plugViewSize.bottom - plugViewSize.top;

	xEmbedInfoAtom = XInternAtom (xDisplay, "_XEMBED_INFO", true);
	if (xEmbedInfoAtom == None)
	{
		std::fprintf (stderr, "_XEMBED_INFO atom unavailable — no XEMBED-capable environment.\n");
		return false;
	}

	int screen = DefaultScreen (xDisplay);

	XVisualInfo vInfo;
	if (!XMatchVisualInfo (xDisplay, screen, 24, TrueColor, &vInfo))
	{
		std::fprintf (stderr, "No 24-bit TrueColor visual available.\n");
		return false;
	}

	XSetWindowAttributes winAttr {};
	winAttr.border_pixel = BlackPixel (xDisplay, screen);
	winAttr.background_pixel = BlackPixel (xDisplay, screen);
	winAttr.colormap =
	    XCreateColormap (xDisplay, XDefaultRootWindow (xDisplay), vInfo.visual, AllocNone);
	unsigned long winAttrMask = CWBackPixel | CWColormap | CWBorderPixel;

	xWindow = XCreateWindow (xDisplay, RootWindow (xDisplay, screen), 0, 0, currentWidth,
	                         currentHeight, 0, vInfo.depth, InputOutput, vInfo.visual, winAttrMask,
	                         &winAttr);
	if (!xWindow)
	{
		std::fprintf (stderr, "XCreateWindow failed.\n");
		return false;
	}

	XSelectInput (xDisplay, xWindow,
	              ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask |
	                  FocusChangeMask);

	auto* sizeHints = XAllocSizeHints ();
	sizeHints->flags = PMinSize;
	if (!resizeable)
	{
		sizeHints->flags |= PMaxSize;
		sizeHints->min_width = sizeHints->max_width = currentWidth;
		sizeHints->min_height = sizeHints->max_height = currentHeight;
	}
	else
	{
		sizeHints->min_width = sizeHints->min_height = 80;
	}
	XSetWMNormalHints (xDisplay, xWindow, sizeHints);
	XFree (sizeHints);

	XStoreName (xDisplay, xWindow, title.c_str ());

	wmDeleteWindowAtom = XInternAtom (xDisplay, "WM_DELETE_WINDOW", False);
	XSetWMProtocols (xDisplay, xWindow, &wmDeleteWindowAtom, 1);

	XSetWindowAttributes plugAttr {};
	plugAttr.override_redirect = True;
	plugAttr.event_mask =
	    ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | KeyPressMask;
	plugAttr.border_pixel = BlackPixel (xDisplay, screen);
	plugAttr.background_pixel = BlackPixel (xDisplay, screen);
	plugAttr.colormap = winAttr.colormap;
	plugParentWindow = XCreateWindow (xDisplay, xWindow, 0, 0, currentWidth, currentHeight, 0,
	                                  vInfo.depth, InputOutput, vInfo.visual,
	                                  CWBackPixel | CWColormap | CWBorderPixel, &plugAttr);

	XSelectInput (xDisplay, plugParentWindow, SubstructureNotifyMask | PropertyChangeMask);
	XMapWindow (xDisplay, plugParentWindow);

	RunLoop::instance ().registerWindow (
	    plugParentWindow, [this] (const XEvent& e) { return handlePlugEvent (e); });
	RunLoop::instance ().registerWindow (
	    xWindow, [this] (const XEvent& e) { return handleMainWindowEvent (e); });

	return true;
}

//------------------------------------------------------------------------
bool EditorWindowX11::show ()
{
	if (!plugView || !xWindow)
		return false;

	self = shared_from_this ();

	if (plugView->isPlatformTypeSupported (kPlatformTypeX11EmbedWindowID) != kResultTrue)
	{
		std::fprintf (stderr, "Plugin editor does not support X11 embedding.\n");
		self = nullptr;
		return false;
	}

	plugView->setFrame (this);

	// Flush our window creation to the server before attaching. A JUCE plugin editor runs on its own
	// X connection and creates its editor as a child of plugParentWindow; without this sync our
	// buffered XCreateWindow can reach the server after the plugin's connection references the parent,
	// giving BadWindow on the plugin's XCreateWindow and a blank editor.
	XSync (xDisplay, False);

	if (plugView->attached (reinterpret_cast<void*> (plugParentWindow),
	                        kPlatformTypeX11EmbedWindowID) != kResultTrue)
	{
		plugView->setFrame (nullptr);
		self = nullptr;
		return false;
	}
	viewAttached = true;

	XMapWindow (xDisplay, xWindow);
	XSync (xDisplay, False);

	dirtyPollTimer = RunLoop::instance ().registerTimer (kDirtyPollIntervalMs,
	                                                     [this] (TimerId) { saveIfDirty (); });
	timerActive = true;

	return true;
}

//------------------------------------------------------------------------
void EditorWindowX11::closePlugView ()
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
void EditorWindowX11::onClose ()
{
	if (timerActive)
	{
		RunLoop::instance ().unregisterTimer (dirtyPollTimer);
		timerActive = false;
	}
	if (pendingSaveTimerActive)
	{
		RunLoop::instance ().unregisterTimer (pendingSaveTimer);
		pendingSaveTimerActive = false;
	}

	// Unconditional final save (design-cli: "final capture and write before exit"). Dormant (no
	// target) no-ops. Matches the Win32 requestClose() behavior: save(), not saveIfDirty().
	if (presetManager && presetManager->hasTarget () && !presetManager->save ())
		std::fprintf (stderr, "Warning: failed to save preset on close; state was not persisted.\n");

	closePlugView ();
	platform::quitEventLoop ();
	self = nullptr;
}

//------------------------------------------------------------------------
void EditorWindowX11::requestClose ()
{
	onClose ();
}

//------------------------------------------------------------------------
void EditorWindowX11::updateTitle ()
{
	if (!xDisplay || !xWindow)
		return;
	std::string title = className;
	if (presetManager && presetManager->hasTarget ())
		title += " — " + fileNameOf (presetManager->targetPath ());
	XStoreName (xDisplay, xWindow, title.c_str ());
	XFlush (xDisplay);
}

//------------------------------------------------------------------------
void EditorWindowX11::postSaveRequest ()
{
	// A save requested from an edit callback (endEdit/restartComponent/setDirty). Defer it onto the
	// run loop via a one-shot timer so it lands on the same code path as the 1s dirty poll, off any
	// reentrant plugin context. Coalesce: one pending post at a time.
	if (pendingSaveTimerActive)
		return;
	pendingSaveTimerActive = true;
	pendingSaveTimer = RunLoop::instance ().registerTimer (1, [this] (TimerId id) {
		RunLoop::instance ().unregisterTimer (id);
		pendingSaveTimerActive = false;
		saveIfDirty ();
	});
}

//------------------------------------------------------------------------
void EditorWindowX11::saveIfDirty ()
{
	if (presetManager && !dialogOpen)
		presetManager->saveIfDirty ();
}

//------------------------------------------------------------------------
void EditorWindowX11::pauseDirtyPoll ()
{
	if (timerActive)
	{
		RunLoop::instance ().unregisterTimer (dirtyPollTimer);
		timerActive = false;
	}
}

//------------------------------------------------------------------------
void EditorWindowX11::resumeDirtyPoll ()
{
	if (!timerActive)
	{
		dirtyPollTimer = RunLoop::instance ().registerTimer (kDirtyPollIntervalMs,
		                                                     [this] (TimerId) { saveIfDirty (); });
		timerActive = true;
	}
}

//------------------------------------------------------------------------
void EditorWindowX11::resizeWindows (int width, int height)
{
	if (xWindow)
		XResizeWindow (xDisplay, xWindow, width, height);
	if (plugParentWindow)
		XResizeWindow (xDisplay, plugParentWindow, width, height);
	currentWidth = width;
	currentHeight = height;
}

//------------------------------------------------------------------------
bool EditorWindowX11::handleMainWindowEvent (const XEvent& event)
{
	switch (event.type)
	{
		case ConfigureNotify:
		{
			if (event.xconfigure.window != xWindow)
				break;
			int width = event.xconfigure.width;
			int height = event.xconfigure.height;
			if (!plugView || (width == currentWidth && height == currentHeight))
				return true;

			// A resizeable plugin may snap the requested size to its own grid via checkSizeConstraint;
			// a plugin that returns anything but kResultTrue keeps the requested size. Either way the
			// plugin is told the final size via onSize UNCONDITIONALLY — matching the Win32 leg, where
			// onSize is not gated on the constraint query (a plugin that returns kNotImplemented from
			// checkSizeConstraint must still be relaid out, or it clips inside a resized window).
			ViewRect r {};
			r.right = width;
			r.bottom = height;
			if (plugView->checkSizeConstraint (&r) == kResultTrue &&
			    (r.right - r.left != width || r.bottom - r.top != height))
			{
				width = r.right - r.left;
				height = r.bottom - r.top;
				resizeWindows (width, height);
			}
			else
			{
				currentWidth = width;
				currentHeight = height;
				if (plugParentWindow)
					XResizeWindow (xDisplay, plugParentWindow, width, height);
			}

			ViewRect onSizeRect {};
			onSizeRect.right = width;
			onSizeRect.bottom = height;
			plugView->onSize (&onSizeRect);
			return true;
		}

		case MapNotify:
			if (event.xany.window == xWindow)
				isMapped = true;
			return true;

		case KeyPress:
		{
			auto keysym = XLookupKeysym (const_cast<XKeyEvent*> (&event.xkey), 0);
			bool ctrl = (event.xkey.state & ControlMask) != 0;
			bool shift = (event.xkey.state & ShiftMask) != 0;
			if (ctrl && (keysym == XK_o || keysym == XK_O))
			{
				onOpenPreset ();
				return true;
			}
			if (ctrl && shift && (keysym == XK_s || keysym == XK_S))
			{
				onSavePresetAs ();
				return true;
			}
			if (ctrl && (keysym == XK_w || keysym == XK_W))
			{
				requestClose ();
				return true;
			}
			return true;
		}

		case ClientMessage:
			if (event.xany.window == xWindow &&
			    static_cast<Atom> (event.xclient.data.l[0]) == wmDeleteWindowAtom)
			{
				requestClose ();
				return true;
			}
			break;

		case FocusIn:
			if (xembedInfo && plugWindow)
				sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_WINDOW_ACTIVATE, 0,
				                   plugParentWindow, xembedInfo->version);
			break;

		case FocusOut:
			if (xembedInfo && plugWindow)
				sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_WINDOW_DEACTIVATE, 0,
				                   plugParentWindow, xembedInfo->version);
			break;
	}
	return false;
}

//------------------------------------------------------------------------
auto EditorWindowX11::getXEmbedInfo () -> XEmbedInfo*
{
	if (xEmbedInfoAtom == None)
		xEmbedInfoAtom = XInternAtom (xDisplay, "_XEMBED_INFO", true);
	int actualFormat;
	unsigned long itemsReturned;
	unsigned long bytesAfterReturn;
	Atom actualType;
	XEmbedInfo* info = nullptr;
	auto err = XGetWindowProperty (xDisplay, plugWindow, xEmbedInfoAtom, 0, 2, false, xEmbedInfoAtom,
	                               &actualType, &actualFormat, &itemsReturned, &bytesAfterReturn,
	                               reinterpret_cast<unsigned char**> (&info));
	if (err != Success)
		return nullptr;
	// The property must exist with the expected type and carry both 32-bit words (version, flags);
	// a missing or malformed property returns Success with a short/typeless result we must not trust.
	if (!info || actualType != xEmbedInfoAtom || actualFormat != 32 || itemsReturned < 2)
	{
		if (info)
			XFree (info);
		return nullptr;
	}
	return info;
}

//------------------------------------------------------------------------
void EditorWindowX11::embedPlugWindow (Window window)
{
	if (plugWindow == window)
		return; // already embedded

	plugWindow = window;

	if (xEmbedAtom == None)
		xEmbedAtom = XInternAtom (xDisplay, "_XEMBED", false);

	xembedInfo = getXEmbedInfo ();

	RunLoop::instance ().registerWindow (plugWindow,
	                                     [this] (const XEvent& e) { return handlePlugEvent (e); });

	uint32_t version = xembedInfo ? xembedInfo->version : 0;
	sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_EMBEDDED_NOTIFY, 0, plugParentWindow,
	                   version);
	XMapWindow (xDisplay, plugWindow);
	XResizeWindow (xDisplay, plugWindow, currentWidth, currentHeight);
	sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_WINDOW_ACTIVATE, 0, plugParentWindow,
	                   version);
	sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_FOCUS_IN, 0, plugParentWindow, version);
	XSync (xDisplay, False);
}

//------------------------------------------------------------------------
bool EditorWindowX11::handlePlugEvent (const XEvent& event)
{
	switch (event.type)
	{
		case ClientMessage:
		{
			if (event.xclient.message_type == xEmbedAtom && xembedInfo)
			{
				if (event.xclient.data.l[1] == XEMBED_REQUEST_FOCUS)
					sendXEmbedMessage (xDisplay, plugWindow, xEmbedAtom, XEMBED_FOCUS_IN, 0,
					                   plugParentWindow, xembedInfo ? xembedInfo->version : 0);
			}
			return true;
		}

		// The plugin editor's window is delivered as a child of our plug-parent window: CreateNotify
		// when the plugin creates it there directly (the common path), ReparentNotify when the plugin
		// creates it elsewhere first and reparents it in. Both are gated on parent == plugParentWindow
		// so an unrelated window (e.g. the plugin's own top-level message window) is never mistaken for
		// the editor.
		case CreateNotify:
			if (event.xcreatewindow.parent == plugParentWindow)
				embedPlugWindow (event.xcreatewindow.window);
			return true;

		case ReparentNotify:
			if (event.xreparent.parent == plugParentWindow)
				embedPlugWindow (event.xreparent.window);
			return true;
	}
	return false;
}

//------------------------------------------------------------------------
void EditorWindowX11::onOpenPreset ()
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
void EditorWindowX11::onSavePresetAs ()
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
tresult PLUGIN_API EditorWindowX11::resizeView (IPlugView* view, ViewRect* newSize)
{
	if (newSize == nullptr || view == nullptr || view != plugView)
		return kInvalidArgument;
	if (!xWindow)
		return kInternalError;
	if (resizeViewRecursionGuard)
		return kResultFalse;

	int width = newSize->right - newSize->left;
	int height = newSize->bottom - newSize->top;
	if (width == currentWidth && height == currentHeight)
		return kResultTrue;

	// A resizeable plugin drives its window size through this callback; keep the top-left anchored by
	// resizing in place. For fixed-size plugins the WM min=max hints already prevent user resizes, so
	// this reflects a plugin-initiated size change (e.g. a UI mode toggle).
	if (!resizeable)
	{
		auto* sizeHints = XAllocSizeHints ();
		sizeHints->flags = PMinSize | PMaxSize;
		sizeHints->min_width = sizeHints->max_width = width;
		sizeHints->min_height = sizeHints->max_height = height;
		XSetWMNormalHints (xDisplay, xWindow, sizeHints);
		XFree (sizeHints);
	}

	resizeViewRecursionGuard = true;
	resizeWindows (width, height);
	resizeViewRecursionGuard = false;

	if (plugView->onSize (newSize) != kResultTrue)
		return kInternalError;
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EditorWindowX11::queryInterface (const TUID iid, void** obj)
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
	if (Steinberg::FUnknownPrivate::iidEqual (iid, Steinberg::Linux::IRunLoop::iid))
	{
		*obj = &RunLoopImpl::instance ();
		return kResultTrue;
	}
	*obj = nullptr;
	return kNoInterface;
}

//------------------------------------------------------------------------
std::shared_ptr<PlatformWindow> makePlatformWindow (const std::string& title,
                                                    const IPtr<IPlugView>& view,
                                                    PresetManager* presetManager)
{
	return EditorWindowX11::make (title, view, presetManager);
}

} // namespace vstdemon
