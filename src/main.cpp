#include <cstdio>
#include <cstring>

namespace {

void printUsage(std::FILE* out) {
	std::fprintf(out,
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

} // namespace

int main(int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--help") == 0) {
			printUsage(stdout);
			return 0;
		}
	}

	printUsage(stdout);
	return 0;
}
