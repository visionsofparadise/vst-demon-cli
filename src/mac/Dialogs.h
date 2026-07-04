#pragma once

#include <optional>
#include <string>

namespace vstdemon {

// Native file dialogs via NSOpenPanel / NSSavePanel, restricted to .vstpreset. The synchronous
// runModal wait is bracketed by the caller's dirty-poll suspension (mirrors the Win32 DialogScope
// and the Linux zenity path). Returns the chosen path, or std::nullopt on cancel.
std::optional<std::string> runOpenPresetDialog (const std::string& suggestedName);
std::optional<std::string> runSavePresetDialog (const std::string& suggestedName);

} // namespace vstdemon
