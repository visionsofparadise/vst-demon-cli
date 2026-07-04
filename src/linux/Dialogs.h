#pragma once

#include <optional>
#include <string>

namespace vstdemon {

// Native file dialogs via a zenity subprocess (kdialog fallback). Filled in by 2.5; the synchronous
// wait suspends the dirty poll exactly like the Win32 DialogScope. Returns the chosen path, or
// std::nullopt on cancel/failure.
std::optional<std::string> runOpenPresetDialog (const std::string& suggestedName);
std::optional<std::string> runSavePresetDialog (const std::string& suggestedName);

} // namespace vstdemon
