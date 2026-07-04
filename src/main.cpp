#include "Platform.h"
#include "PluginHost.h"
#include "PresetManager.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void printUsage (std::FILE* out)
{
	std::fprintf (
	    out,
	    "vst-demon --plugin <path.vst3> [--plugin-name <shell-sub-plugin>] [--preset <path.vstpreset>]\n"
	    "vst-demon --plugin <path.vst3> --list\n"
	    "\n"
	    "Opens a VST3 plugin's editor GUI in a bare host window and continuously\n"
	    "saves the plugin's state to a .vstpreset file.\n"
	    "\n"
	    "Options:\n"
	    "  --plugin <path>         Path to the .vst3 module (required).\n"
	    "  --plugin-name <name>    Select a sub-plugin class by name (shell plugins).\n"
	    "  --preset <path>         Preset file to load and auto-save to.\n"
	    "  --list                  Print the module's audio-effect class names and exit.\n"
	    "  --help                  Print this message and exit.\n");
}

std::string jsonEscape (const std::string& s)
{
	std::string out;
	out.reserve (s.size () + 2);
	for (char c : s)
	{
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char> (c) < 0x20)
				{
					char buf[8];
					std::snprintf (buf, sizeof (buf), "\\u%04x", c);
					out += buf;
				}
				else
					out += c;
		}
	}
	return out;
}

// The stdout event stream (design-cli's CLI contract): one line of JSON per event, nothing else on
// stdout; all diagnostics go to stderr. Paths are the only dynamic strings, hand-rolled-escaped.
void emitEvent (const std::string& name)
{
	std::printf ("{\"event\":\"%s\"}\n", name.c_str ());
	std::fflush (stdout);
}

void emitPathEvent (const std::string& name, const std::string& path)
{
	std::printf ("{\"event\":\"%s\",\"path\":\"%s\"}\n", name.c_str (), jsonEscape (path).c_str ());
	std::fflush (stdout);
}

struct Args
{
	std::string plugin;
	std::string pluginName;
	std::string preset;
	bool list {false};
	bool help {false};
};

bool takeValue (int argc, char* argv[], int& i, const char* flag, std::string& out)
{
	if (i + 1 >= argc)
	{
		std::fprintf (stderr, "Missing value for %s\n", flag);
		return false;
	}
	out = argv[++i];
	return true;
}

} // namespace

int main (int argc, char* argv[])
{
	Args args;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp (argv[i], "--help") == 0)
			args.help = true;
		else if (std::strcmp (argv[i], "--list") == 0)
			args.list = true;
		else if (std::strcmp (argv[i], "--plugin") == 0)
		{
			if (!takeValue (argc, argv, i, "--plugin", args.plugin))
				return 1;
		}
		else if (std::strcmp (argv[i], "--plugin-name") == 0)
		{
			if (!takeValue (argc, argv, i, "--plugin-name", args.pluginName))
				return 1;
		}
		else if (std::strcmp (argv[i], "--preset") == 0)
		{
			if (!takeValue (argc, argv, i, "--preset", args.preset))
				return 1;
		}
		else
		{
			std::fprintf (stderr, "Unknown argument: %s\n", argv[i]);
			return 1;
		}
	}

	if (args.help)
	{
		printUsage (stdout);
		return 0;
	}

	if (args.plugin.empty ())
	{
		std::fprintf (stderr, "Error: --plugin <path.vst3> is required.\n\n");
		printUsage (stderr);
		return 1;
	}

	if (args.list)
	{
		vstdemon::PluginHost host;
		auto loaded = host.loadModule (args.plugin);
		if (!loaded.ok)
		{
			std::fprintf (stderr, "%s\n", loaded.error.c_str ());
			return 1;
		}
		auto names = host.effectClassNames ();
		if (names.empty ())
		{
			std::fprintf (stderr, "%s\n", vstdemon::PluginHost::emptyFactoryError ());
			return 1;
		}
		std::string json = "[";
		for (size_t i = 0; i < names.size (); ++i)
		{
			if (i != 0)
				json += ",";
			json += "\"" + jsonEscape (names[i]) + "\"";
		}
		json += "]";
		std::printf ("%s\n", json.c_str ());
		return 0;
	}

	vstdemon::platform::initialize ();

	{
		vstdemon::PluginHost host;
		auto opened = host.open (args.plugin, args.pluginName);
		if (!opened.ok)
		{
			std::fprintf (stderr, "%s\n", opened.error.c_str ());
			vstdemon::platform::terminate ();
			return 1;
		}

		Steinberg::FUID componentUID;
		if (!host.getComponentUID (componentUID))
		{
			std::fprintf (stderr, "Could not resolve the component class ID for '%s'.\n",
			              host.selectedClassName ().c_str ());
			vstdemon::platform::terminate ();
			return 1;
		}

		vstdemon::PresetManager presetManager (host.getComponent (), host.getController (),
		                                       componentUID);
		presetManager.setOnSaved ([] (const std::string& path) { emitPathEvent ("saved", path); });
		presetManager.setTarget (args.preset);

		auto prepared = presetManager.prepareTarget ();
		if (!prepared.ok)
		{
			std::fprintf (stderr, "%s\n", prepared.error.c_str ());
			vstdemon::platform::terminate ();
			return 1;
		}

		auto loaded2 = presetManager.load ();
		if (!loaded2.ok)
		{
			std::fprintf (stderr, "%s\n", loaded2.error.c_str ());
			vstdemon::platform::terminate ();
			return 1;
		}

		auto view = host.createView ();
		if (!view)
		{
			std::fprintf (stderr, "Plugin '%s' does not provide an editor view.\n",
			              host.selectedClassName ().c_str ());
			vstdemon::platform::terminate ();
			return 1;
		}

		// Declared after host (and presetManager): reverse-order destruction tears the window and its
		// handler down before the provider, so no controller callback can reach a dead window.
		auto window = vstdemon::makePlatformWindow (host.selectedClassName (), view, &presetManager);
		if (!window)
		{
			std::fprintf (stderr, "Failed to create editor window.\n");
			vstdemon::platform::terminate ();
			return 1;
		}

		// Route edit-triggered saves through the window's run-loop post (replaces the raw HWND handoff);
		// every save lands on the single run-loop path shared with the 1s dirty poll.
		vstdemon::PlatformWindow* windowPtr = window.get ();
		host.componentHandler ().setSaveRequest ([windowPtr] { windowPtr->postSaveRequest (); });

		if (!window->show ())
		{
			std::fprintf (stderr, "Failed to attach the plugin editor to the window.\n");
			window->closePlugView ();
			vstdemon::platform::terminate ();
			return 1;
		}

		emitEvent ("ready");

		// Retarget updates the window title only (Open and Save As both retarget). The stdout events
		// are separate: "open" fires on opens (startup --preset + File > Open Preset), "saved" on every
		// write. Wired after the window exists so updateTitle has a valid window.
		presetManager.setOnRetarget ([windowPtr] (const std::string&) { windowPtr->updateTitle (); });
		presetManager.setOnOpened ([] (const std::string& path) { emitPathEvent ("open", path); });
		window->updateTitle ();

		// Emit the startup "open" for the initial --preset (existing or not). After ready, so stdout
		// order is ready -> open. Dormant launch (no --preset): announceTarget no-ops.
		presetManager.announceTarget ();

		vstdemon::platform::runEventLoop ();

		emitEvent ("closed");
	}

	vstdemon::platform::terminate ();
	return 0;
}
