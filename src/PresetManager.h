#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <functional>
#include <string>
#include <vector>

namespace vstdemon {

struct PresetResult
{
	bool ok {false};
	std::string error;
};

// Owns the active save target and load/save of the plugin state to a .vstpreset file.
// The save target is the most recently opened or saved path; both are carried back to the caller
// on stdout (the "open" and "saved" events), so the current target is observable. Every write is
// atomic (temp file + MoveFileEx replace). Last-written component and controller state bytes are
// retained so saveIfDirty() can skip no-op writes.
class PresetManager
{
public:
	PresetManager (Steinberg::Vst::IComponent* component,
	               Steinberg::Vst::IEditController* controller, const Steinberg::FUID& componentUID);

	PresetManager (const PresetManager&) = delete;
	PresetManager& operator= (const PresetManager&) = delete;

	// The active save target, "" when dormant (no --preset and no Save As yet).
	const std::string& targetPath () const { return target; }
	bool hasTarget () const { return !target.empty (); }

	// Retarget the active save target. On a real change (path differs) the onRetarget callback
	// fires to refresh UI (the window title). This is title-only — the stdout events are "open"
	// (opens) and "saved" (writes), fired at their own sites. No-ops when the path is unchanged.
	void setTarget (const std::string& path);

	// Emit the startup "open" event for the initial --preset target (onOpened), whether or not the
	// file exists yet. Used once at startup after the ready event, so stdout order stays
	// ready -> open. No target (dormant launch): no-op.
	void announceTarget ();

	// Ensure the current target's parent directory exists (create it recursively) so auto-save can
	// write there. Called once at startup for the initial --preset; returns ok=false with a clear
	// message if the directory cannot be created, letting the caller fail fast before opening the
	// editor instead of surfacing a cryptic write error mid-session. No target: no-op, ok=true.
	PresetResult prepareTarget ();

	// Invoked with the target path after every successful write. Wired by main.cpp to emit the
	// stdout {"event":"saved","path":...} event; the emitter stays in main.cpp scope.
	void setOnSaved (std::function<void (const std::string&)> callback)
	{
		onSaved = std::move (callback);
	}

	// Invoked with the new target path on every retarget (Open, Save As). Wired by main.cpp to
	// update the window title. Title only — no stdout event; the events are onOpened / onSaved.
	void setOnRetarget (std::function<void (const std::string&)> callback)
	{
		onRetarget = std::move (callback);
	}

	// Invoked with the path when the target is opened: at startup for the initial --preset (existing
	// or not, via announceTarget) and on File > Open Preset. Wired by main.cpp to emit
	// {"event":"open","path":...}. Does NOT fire on Save As (that emits only saved).
	void setOnOpened (std::function<void (const std::string&)> callback)
	{
		onOpened = std::move (callback);
	}

	// Load target into the plugin if the file exists. Missing file: no load, target unchanged,
	// ok=true. Unreadable / wrong-class file: ok=false with an error. No target: no-op, ok=true.
	PresetResult load ();

	// Load an explicit file into the live component/controller and retarget to it (File > Open
	// Preset...). On success the loaded state is active, auto-save targets the new path (onRetarget
	// fires for the title), and onOpened fires (the "open" event). On failure the current session
	// and target are left unchanged.
	PresetResult openPreset (const std::string& path);

	// Atomic write of the current plugin state to the target. No target: no-op returning false.
	// On success, captures the written state as the last-written baseline.
	bool save ();

	// Write the current state to an explicit path and, only on success, retarget to it (File > Save
	// Preset As...). Symmetric with openPreset: a failed write leaves the target untouched. On
	// success fires onRetarget (title only) then onSaved (the "saved" event). Does NOT fire onOpened
	// — Save As is a write, not an open; its path rides on the saved event alone.
	bool saveAs (const std::string& path);

	// Capture fresh state, byte-compare to last-written; write only on difference. No target or
	// no change: returns false. Serves the Phase 4 dirty poll and edit-triggered saves.
	bool saveIfDirty ();

private:
	// Load a specific file into the live component/controller and refresh the last-written baseline.
	// Shared by load() (startup --preset) and openPreset() (File > Open Preset...).
	PresetResult loadFile (const std::string& path);
	bool captureState (std::vector<char>& componentBytes, std::vector<char>& controllerBytes) const;
	// Atomic write of the current state to an explicit path (temp + MoveFileEx). Refreshes the
	// last-written baseline; does NOT fire onSaved (the caller does, after any retarget).
	bool writePreset (const std::string& path);

	Steinberg::Vst::IComponent* component;
	Steinberg::Vst::IEditController* controller;
	Steinberg::FUID componentUID;

	std::string target;
	std::function<void (const std::string&)> onSaved;
	std::function<void (const std::string&)> onRetarget;
	std::function<void (const std::string&)> onOpened;

	bool haveLastWritten {false};
	std::vector<char> lastComponentState;
	std::vector<char> lastControllerState;
};

} // namespace vstdemon
