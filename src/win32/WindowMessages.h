#pragma once

#include <windows.h>

namespace vstdemon {

// Posted to the editor window whenever a plugin edit callback (endEdit / restartComponent /
// setDirty) requests a save. Handled on the message loop so every save runs on the same single
// code path as the 1s dirty-poll timer (WM_TIMER). See design-cli's auto-save trigger set.
constexpr UINT WM_VSTDEMON_SAVE = WM_APP + 1;

} // namespace vstdemon
