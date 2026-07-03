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
	void setTarget (const std::string& path) { target = path; }

	// Invoked with the target path after every successful write. Wired by main.cpp to emit the
	// stdout {"event":"saved","path":...} event; the emitter stays in main.cpp scope.
	void setOnSaved (std::function<void (const std::string&)> callback)
	{
		onSaved = std::move (callback);
	}

	// Load target into the plugin if the file exists. Missing file: no load, target unchanged,
	// ok=true. Unreadable / wrong-class file: ok=false with an error. No target: no-op, ok=true.
	PresetResult load ();

	// Atomic write of the current plugin state to the target. No target: no-op returning false.
	// On success, captures the written state as the last-written baseline.
	bool save ();

	// Capture fresh state, byte-compare to last-written; write only on difference. No target or
	// no change: returns false. Serves the Phase 4 dirty poll and edit-triggered saves.
	bool saveIfDirty ();

private:
	bool captureState (std::vector<char>& componentBytes, std::vector<char>& controllerBytes) const;
	bool writePreset ();

	Steinberg::Vst::IComponent* component;
	Steinberg::Vst::IEditController* controller;
	Steinberg::FUID componentUID;

	std::string target;
	std::function<void (const std::string&)> onSaved;

	bool haveLastWritten {false};
	std::vector<char> lastComponentState;
	std::vector<char> lastControllerState;
};

} // namespace vstdemon
