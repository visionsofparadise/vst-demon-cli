#pragma once

#include "../Platform.h"
#include "RunLoop.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <memory>
#include <string>

#include <X11/Xlib.h>

namespace vstdemon {

class PresetManager;

using Steinberg::FUnknown;
using Steinberg::IPlugFrame;
using Steinberg::IPlugView;
using Steinberg::IPtr;
using Steinberg::TUID;
using Steinberg::ViewRect;

// The Linux host window: a top-level X11 window with an override-redirect child (plugParentWindow)
// the plugin editor attaches to as kPlatformTypeX11EmbedWindowID, driving the XEMBED handshake. The
// same shape as the Win32 EditorWindow — it is the plugin's IPlugFrame, owns its dirty-poll timer,
// and posts saves onto the run loop — but over X11 and the vstdemon::RunLoop instead of Win32
// messages. queryInterface serves the plugin's IRunLoop fallback. Adapted from the SDK editorhost
// sample's X11Window.
class EditorWindowX11 : public PlatformWindow,
                        public IPlugFrame,
                        public std::enable_shared_from_this<EditorWindowX11>
{
public:
	static std::shared_ptr<EditorWindowX11> make (const std::string& title, const IPtr<IPlugView>& view,
	                                              PresetManager* presetManager);

	~EditorWindowX11 () noexcept override;

	bool show () override;
	void closePlugView () override;
	void updateTitle () override;
	void postSaveRequest () override;

	// IPlugFrame
	Steinberg::tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;

	// FUnknown — serves IPlugFrame and the Linux::IRunLoop the plugin queries as the IPlugFrame
	// fallback. Stub refcount; lifetime is owned by the shared_ptr self-reference.
	Steinberg::tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef () override { return 1000; }
	Steinberg::uint32 PLUGIN_API release () override { return 1000; }

private:
	bool init (const std::string& title, const IPtr<IPlugView>& view);

	bool handleMainWindowEvent (const XEvent& event);
	bool handlePlugEvent (const XEvent& event);

	void onClose ();
	void requestClose ();

	// File actions (2.5): zenity dialogs, dirty-poll suspended around the synchronous wait.
	void onOpenPreset ();
	void onSavePresetAs ();

	// The XEMBED _XEMBED_INFO property of the plugin's mapped window.
	struct XEmbedInfo
	{
		uint32_t version;
		uint32_t flags;
	};
	XEmbedInfo* getXEmbedInfo ();

	// Run the XEMBED handshake for a plugin window that has appeared under plugParentWindow. Idempotent
	// once embedded. Triggered by whichever notify arrives first — CreateNotify (plugin creates its
	// window directly under the parent) or ReparentNotify/MapNotify (VSTGUI-class plugins create at the
	// root and reparent in, so no CreateNotify reaches us).
	void embedPlugWindow (Window window);

	void resizeWindows (int width, int height);

	void saveIfDirty ();

	// 1s dirty-poll suspension around a modal dialog (2.5), mirroring the Win32 DialogScope.
	void pauseDirtyPoll ();
	void resumeDirtyPoll ();

	IPtr<IPlugView> plugView;
	PresetManager* presetManager {nullptr};
	std::string className;

	Display* xDisplay {nullptr};
	Window xWindow {0};
	Window plugParentWindow {0};
	Window plugWindow {0};
	Atom xEmbedInfoAtom {None};
	Atom xEmbedAtom {None};
	Atom wmDeleteWindowAtom {None};
	XEmbedInfo* xembedInfo {nullptr};

	int currentWidth {0};
	int currentHeight {0};
	bool resizeable {false};
	bool isMapped {false};

	TimerId dirtyPollTimer {0};
	bool timerActive {false};
	bool pendingSaveTimerActive {false};
	TimerId pendingSaveTimer {0};
	bool dialogOpen {false};
	bool viewAttached {false};
	bool resizeViewRecursionGuard {false};

	std::shared_ptr<EditorWindowX11> self;
};

} // namespace vstdemon
