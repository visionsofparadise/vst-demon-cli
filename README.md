# vst-demon

A `.vstpreset` editor for Windows. `vst-demon` opens a VST3 plugin's own GUI in a bare host window and continuously saves the plugin's state to a `.vstpreset` file as you edit. There is no audio path — it hosts the editor only. Close the window and the state is saved; reopen with the same file and the exact state is restored.

It fills a gap: authoring `.vstpreset` files otherwise requires a DAW or Steinberg's VST3PluginTestHost. `vst-demon` needs neither — point it at a `.vst3`, turn knobs, close.

## Installation

Two artifacts are attached to each [release](https://github.com/visionsofparadise/vst-demon-cli/releases):

- **Installer** — `vst-demon-setup-<version>.exe`. Installs `vst-demon.exe` to `C:\Program Files\ZCROSS\VST Demon\` and adds that directory to your system `PATH`, so `vst-demon` works from any shell. The uninstaller removes both. (Requires administrator rights; open a new shell after installing so the updated PATH is picked up.)
- **Portable** — `vst-demon-win32-x64.zip`. Unzip anywhere and run `vst-demon.exe` directly. No install, no PATH change, no admin. This is the artifact to embed in another application.

### SmartScreen

The binary is unsigned, so Windows SmartScreen shows a "Windows protected your PC" prompt on first run of the installer or the exe. To run it: click **More info**, then **Run anyway**. (You can also right-click the file → **Properties** → check **Unblock** → **OK** before running.)

## Usage

```
vst-demon --plugin <path.vst3> [--plugin-name <shell-sub-plugin>] [--preset <path.vstpreset>]
vst-demon --plugin <path.vst3> --list
```

- `--plugin <path.vst3>` — the plugin to open (required).
- `--plugin-name <name>` — for shell plugins that expose several sub-plugins in one file (e.g. Waves WaveShell), the class name to open. Use `--list` to discover the names. Omit for single-plugin files.
- `--preset <path.vstpreset>` — the preset file to load at startup and auto-save to. If the file exists its state is restored; if not, it is created on the first save. The parent directory is created recursively if it does not exist, so you can point `--preset` at a not-yet-existing folder; if the directory genuinely cannot be created, `vst-demon` fails fast with a clear message before opening the editor. Omit to open the plugin at its default state (auto-save stays dormant until you assign a path via **File > Save Preset As...**).
- `--list` — print the plugin's audio-effect class names as a JSON array and exit, without opening a window. This is how you discover shell sub-plugin names.
- `--help` — print usage and exit.

Example — open OTT and auto-save to a preset:

```
vst-demon --plugin "C:\Program Files\Common Files\VST3\OTT.vst3" --preset "%USERPROFILE%\Documents\my-ott.vstpreset"
```

Example — list the sub-plugins in a Waves shell:

```
vst-demon --plugin "C:\Program Files\Common Files\VST3\WaveShell3-VST3 10.0_x64.vst3" --list
["REQ 2 Mono","REQ 2 Stereo","REQ 4 Mono","REQ 4 Stereo","REQ 6 Mono","REQ 6 Stereo"]
```

Listing and opening a shell's sub-plugins works for any vendor's shell, but Waves *state* does not round-trip through a standard `.vstpreset` — see [Limitations](#limitations).

### Auto-save

No explicit save is needed. Once a preset path is set (via `--preset` or **Save Preset As...**), the plugin's state is written to that file whenever a knob gesture completes, the plugin marks its state dirty, a 1-second dirty poll detects a change, or the window closes. Writes are atomic. The file you are looking at is always the file being written — opens (startup / Open) and writes are both announced on stdout.

### Menu

**File > Open Preset...** and **Save Preset As...** open native file dialogs and retarget auto-save to the chosen file. **File > Close** exits after a final save.

## stdout event contract

`vst-demon` emits line-delimited JSON to stdout — one object per line, nothing else. All logging and diagnostics go to stderr, so stdout is a clean event stream a parent process can parse. The file on disk is the source of truth; the tool is fully functional with stdout ignored.

| Event                            | When                                                                                               |
| -------------------------------- | -------------------------------------------------------------------------------------------------- |
| `{"event":"ready"}`              | the editor window is up                                                                            |
| `{"event":"open","path":...}`    | the auto-save target was opened: at startup when `--preset` is given (even if the file does not exist yet), and on **File > Open Preset** |
| `{"event":"saved","path":...}`   | a write to the preset file completed (edit / poll / first create / close, and **Save Preset As**)  |
| `{"event":"closed"}`             | before a clean exit                                                                                |

`open` fires only on opens. **Save Preset As** does not emit `open` — it is a write, so it emits `saved` alone (carrying the new path). Both `open` and `saved` carry the current target path, so a parent can track it from either.

Paths are JSON-escaped, so Windows backslashes appear doubled. A typical session — start with `--preset`, edit, then Save As to a new file:

```json
{"event":"ready"}
{"event":"open","path":"C:\\Users\\me\\Documents\\my-ott.vstpreset"}
{"event":"saved","path":"C:\\Users\\me\\Documents\\my-ott.vstpreset"}
{"event":"saved","path":"C:\\Users\\me\\Documents\\other.vstpreset"}
{"event":"closed"}
```

`--list` prints a single JSON array (not an event object) and exits:

```json
["OTT"]
```

### Exit codes

`0` on a clean close. Nonzero, with a message on stderr, for:

- an unknown or malformed argument;
- a missing or unloadable plugin file;
- an unknown `--plugin-name` class;
- a plugin that provides no editor view;
- an unloadable preset (unreadable, or authored for a different plugin class);
- the preset's parent directory could not be created;
- an empty plugin factory — this gets its own message, since for a Waves shell it means the Waves license is inactive in Waves Central.

## Limitations

- **Waves shell plugins.** Sub-plugins enumerate (`--list`) and open (`--plugin-name`) correctly, but their state does not round-trip through a standard `.vstpreset`: reopening a `vst-demon`-written Waves preset resets the plugin to its default, because the Waves component's `setState` does not reconstruct edited state from the standard VST3 component chunk (the same limitation reproduces in Pedalboard). The container `vst-demon` writes is structurally valid — this is a Waves property, not a writer defect. Author Waves presets in a Waves-aware host.
- **Windows x64 only.** The editor window and message loop are the platform-specific surface; a macOS/Linux port would be additive but is not built.

## License

MIT. `vst-demon` is an open utility built on Steinberg's MIT-licensed [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (v3.8.0). VST is a trademark of Steinberg Media Technologies GmbH.
