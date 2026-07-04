#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <memory>
#include <string>

namespace vstdemon {

class PresetManager;

// The platform seam. Shared code (main.cpp, PluginHost) speaks only to these; each platform
// (src/win32, later src/mac, src/linux) provides the implementation. The shape mirrors what the
// Win32 EditorWindow already exposes plus the process/run-loop/context hooks the macOS and Linux
// legs need, so those legs add files without reshaping this header or editing shared code.

// The host window that shows the plugin editor and owns the save-request post. Each platform's
// concrete window is the plugin's IPlugFrame and drives its own resize/dirty-poll/dialog handling
// internally; shared code holds only this abstract surface.
class PlatformWindow
{
public:
	virtual ~PlatformWindow () = default;

	// Attach the plugin editor and show the window. False on failure (editor not attachable).
	virtual bool show () = 0;

	// Detach and release the plugin view. Idempotent.
	virtual void closePlugView () = 0;

	// Refresh the title bar from the current preset target (main.cpp's onRetarget wiring).
	virtual void updateTitle () = 0;

	// Ask the run loop to run a save, off any nested/reentrant context. Replaces the raw
	// PostMessage(hwnd, WM_VSTDEMON_SAVE, ...) coupling: the ComponentHandler's save callback calls
	// this so every save lands on the single run-loop code path shared with the dirty poll.
	virtual void postSaveRequest () = 0;
};

// Create the platform's editor window for the given plugin view. Implemented per platform.
// shared_ptr, not unique_ptr: the Win32 window keeps a self-reference alive across the plugin's
// IPlugFrame callbacks (enable_shared_from_this), so shared ownership is the natural fit; other
// platforms simply make_shared their concrete window.
// closeAfterMs > 0 arms a one-shot timer that drives the platform's normal user-close path (final
// save, closed event) after that many milliseconds — the integration-test hook behind the
// undocumented --close-after-ms flag. 0 disables it.
std::shared_ptr<PlatformWindow> makePlatformWindow (const std::string& title,
                                                    const Steinberg::IPtr<Steinberg::IPlugView>& view,
                                                    PresetManager* presetManager, int closeAfterMs);

namespace platform {

// Process-level init/teardown around the session. Win32: OleInitialize / OleUninitialize. macOS:
// NSApplication bootstrap. Linux: no-op. Paired; initialize() before any window, terminate() after.
void initialize ();
void terminate ();

// Run the platform event loop until quitEventLoop() is called; then return so teardown can run.
// Replaces the raw GetMessage pump in main.cpp.
void runEventLoop ();

// Signal the event loop to return. Called by the platform window on close (Win32: PostQuitMessage).
void quitEventLoop ();

// The FUnknown the plugin factory's host context should expose for platform interfaces (Linux:
// Steinberg::Linux::IRunLoop). nullptr means "no platform context" (Win32, macOS) — the host then
// skips factory.setHostContext, leaving behavior identical to today. PluginHost consults this so the
// Linux leg serves IRunLoop through the factory context without editing shared files.
Steinberg::FUnknown* getPluginFactoryContext ();

} // namespace platform

} // namespace vstdemon
