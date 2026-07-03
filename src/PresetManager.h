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
// The save target is the most recently loaded or assigned path (the preset-path invariant);
// every write is atomic (temp file + MoveFileEx replace). Last-written component and
// controller state bytes are retained so saveIfDirty() can skip no-op writes.
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
	// fires — the single announcement site for the preset-path invariant (design-cli: "every
	// retarget is announced on stdout"). No-ops when the path is unchanged.
	void setTarget (const std::string& path);

	// Announce the current target through onRetarget without changing it. Used once at startup to
	// route the initial --preset target through the same single announcement path, after the
	// ready event has been emitted (so stdout order stays ready -> preset-path).
	void announceTarget ();

	// Invoked with the target path after every successful write. Wired by main.cpp to emit the
	// stdout {"event":"saved","path":...} event; the emitter stays in main.cpp scope.
	void setOnSaved (std::function<void (const std::string&)> callback)
	{
		onSaved = std::move (callback);
	}

	// Invoked with the new target path on every retarget (and the initial --preset via
	// announceTarget). Wired by main.cpp to emit {"event":"preset-path",...} and update the window
	// title. The single announcement site for the preset-path invariant.
	void setOnRetarget (std::function<void (const std::string&)> callback)
	{
		onRetarget = std::move (callback);
	}

	// Load target into the plugin if the file exists. Missing file: no load, target unchanged,
	// ok=true. Unreadable / wrong-class file: ok=false with an error. No target: no-op, ok=true.
	PresetResult load ();

	// Load an explicit file into the live component/controller and retarget to it (File > Open
	// Preset...). On success the loaded state is active and auto-save targets the new path (the
	// onRetarget callback fires). On failure the current session and target are left unchanged.
	PresetResult openPreset (const std::string& path);

	// Atomic write of the current plugin state to the target. No target: no-op returning false.
	// On success, captures the written state as the last-written baseline.
	bool save ();

	// Write the current state to an explicit path and, only on success, retarget to it (File > Save
	// Preset As...). Symmetric with openPreset: a failed write leaves the target and the
	// announcement untouched. On success fires onRetarget (preset-path) then onSaved (saved).
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

	bool haveLastWritten {false};
	std::vector<char> lastComponentState;
	std::vector<char> lastControllerState;
};

} // namespace vstdemon
