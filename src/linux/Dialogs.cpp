#include "Dialogs.h"

#include <array>
#include <cstdio>
#include <cstdlib>

#include <sys/wait.h>

namespace vstdemon {

namespace {

// zenity exits nonzero on cancel and when absent; kdialog likewise. Any nonzero exit → nullopt.
std::optional<std::string> runDialogCommand (const std::string& command)
{
	std::FILE* pipe = popen (command.c_str (), "r");
	if (!pipe)
		return std::nullopt;

	std::string out;
	std::array<char, 512> buf;
	size_t n;
	while ((n = std::fread (buf.data (), 1, buf.size (), pipe)) > 0)
		out.append (buf.data (), n);

	int status = pclose (pipe);
	if (status == -1 || !WIFEXITED (status) || WEXITSTATUS (status) != 0)
		return std::nullopt;

	while (!out.empty () && (out.back () == '\n' || out.back () == '\r'))
		out.pop_back ();
	if (out.empty ())
		return std::nullopt;
	return out;
}

// Single-quote a value for the shell: wrap in '...', escaping any embedded quote as '\''.
std::string shellQuote (const std::string& s)
{
	std::string out = "'";
	for (char c : s)
	{
		if (c == '\'')
			out += "'\\''";
		else
			out += c;
	}
	out += "'";
	return out;
}

bool haveCommand (const char* name)
{
	std::string probe = std::string ("command -v ") + name + " >/dev/null 2>&1";
	return std::system (probe.c_str ()) == 0;
}

} // namespace

//------------------------------------------------------------------------
std::optional<std::string> runOpenPresetDialog (const std::string& suggestedName)
{
	if (haveCommand ("zenity"))
		return runDialogCommand (
		    "zenity --file-selection --title='Open Preset' "
		    "--file-filter='VST3 Preset (*.vstpreset) | *.vstpreset' --file-filter='All files | *' "
		    "--filename=" +
		    shellQuote (suggestedName) + " 2>/dev/null");

	if (haveCommand ("kdialog"))
		return runDialogCommand ("kdialog --getopenfilename " + shellQuote (suggestedName) +
		                         " '*.vstpreset|VST3 Preset' --title 'Open Preset' 2>/dev/null");

	std::fprintf (stderr, "No file dialog available: install zenity or kdialog to open presets.\n");
	return std::nullopt;
}

//------------------------------------------------------------------------
std::optional<std::string> runSavePresetDialog (const std::string& suggestedName)
{
	if (haveCommand ("zenity"))
		return runDialogCommand (
		    "zenity --file-selection --save --confirm-overwrite --title='Save Preset As' "
		    "--file-filter='VST3 Preset (*.vstpreset) | *.vstpreset' --file-filter='All files | *' "
		    "--filename=" +
		    shellQuote (suggestedName) + " 2>/dev/null");

	if (haveCommand ("kdialog"))
		return runDialogCommand ("kdialog --getsavefilename " + shellQuote (suggestedName) +
		                         " '*.vstpreset|VST3 Preset' --title 'Save Preset As' 2>/dev/null");

	std::fprintf (stderr, "No file dialog available: install zenity or kdialog to save presets.\n");
	return std::nullopt;
}

} // namespace vstdemon
