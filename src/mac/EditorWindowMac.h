#pragma once

#include "../Platform.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <memory>
#include <string>

#ifdef __OBJC__
@class NSWindow;
@class VSTDemonWindowDelegate;
@class NSTimer;
#else
using NSWindow = struct objc_object;
using VSTDemonWindowDelegate = struct objc_object;
using NSTimer = struct objc_object;
#endif

namespace vstdemon {

class PresetManager;

using Steinberg::FUnknown;
using Steinberg::IPlugFrame;
using Steinberg::IPlugView;
using Steinberg::IPtr;
using Steinberg::TUID;
using Steinberg::ViewRect;

// The macOS host window: an NSWindow whose contentView the plugin editor attaches to as
// kPlatformTypeNSView. Same shape as the Win32 EditorWindow and the Linux EditorWindowX11 — it is
// the plugin's IPlugFrame, owns its 1s dirty-poll NSTimer, and posts saves onto the main queue. A
// VSTDemonWindowDelegate forwards windowWillResize/windowDidResize/windowWillClose to this object.
// Behavior is build-verified only (no Mac hardware); adapted from the SDK editorhost mac sample.
class EditorWindowMac : public PlatformWindow,
                        public IPlugFrame,
                        public std::enable_shared_from_this<EditorWindowMac>
{
public:
	static std::shared_ptr<EditorWindowMac> make (const std::string& title, const IPtr<IPlugView>& view,
	                                              PresetManager* presetManager, int closeAfterMs);

	~EditorWindowMac () noexcept override;

	bool show () override;
	void closePlugView () override;
	void updateTitle () override;
	void postSaveRequest () override;

	// IPlugFrame
	Steinberg::tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;

	// FUnknown — serves IPlugFrame. Stub refcount; lifetime is owned by the shared_ptr self-reference.
	Steinberg::tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef () override { return 1000; }
	Steinberg::uint32 PLUGIN_API release () override { return 1000; }

	// Delegate callbacks (invoked from VSTDemonWindowDelegate). windowWillResize maps onto
	// checkSizeConstraint (returns the constrained content size), windowDidResize onto onSize.
	void onWillResize (double width, double height, double* outWidth, double* outHeight);
	void onDidResize (double width, double height);
	void onWindowWillClose ();

	// File-menu actions, routed from the NSMenu items (⌘O / ⇧⌘S / ⌘W) via AppMac's shared window.
	void onOpenPreset ();
	void onSavePresetAs ();
	void requestClose ();

private:
	bool init (const std::string& title, const IPtr<IPlugView>& view);

	void saveIfDirty ();

	// 1s dirty-poll suspension around a modal file dialog, mirroring the Win32 DialogScope.
	void pauseDirtyPoll ();
	void resumeDirtyPoll ();

	IPtr<IPlugView> plugView;
	PresetManager* presetManager {nullptr};
	std::string className;

	NSWindow* nsWindow {nullptr};
	VSTDemonWindowDelegate* nsWindowDelegate {nullptr};
	NSTimer* dirtyPollTimer {nullptr};
	NSTimer* closeAfterTimer {nullptr};
	int closeAfterMs {0};

	bool resizeable {false};
	bool viewAttached {false};
	bool dialogOpen {false};
	bool timerActive {false};
	bool resizeViewRecursionGuard {false};

	std::shared_ptr<EditorWindowMac> self;
};

// The window main.cpp created, exposed so AppMac's menu actions (⌘O/⇧⌘S/⌘W) route to it. Set when
// the window is shown, cleared when it closes. There is only ever one editor window per process.
void setActiveMacWindow (EditorWindowMac* window);
EditorWindowMac* activeMacWindow ();

} // namespace vstdemon
