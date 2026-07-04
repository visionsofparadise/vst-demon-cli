<p align="center">
  <img src="assets/vst-demon-logo-spin.webp" alt="VST Demon" width="440">
</p>

# vst-demon-cli

A `.vstpreset` editor. `vst-demon-cli` opens a VST3 plugin's own GUI in a bare host window and continuously saves the plugin's state to a `.vstpreset` file as you edit. There is no audio path — it hosts the editor only. Close the window and the state is saved; reopen with the same file and the exact state is restored.

## Installation

Each [release](https://github.com/visionsofparadise/vst-demon-cli/releases) attaches per-platform assets.

### Windows

- **Installer** — `vst-demon-cli-setup-<version>.exe`. Installs `vst-demon-cli.exe` to `C:\Program Files\ZCROSS\VST Demon CLI\` and adds that directory to your system `PATH`, so `vst-demon-cli` works from any shell. The uninstaller removes both. (Requires administrator rights; open a new shell after installing so the updated PATH is picked up.)

- **Portable** — `vst-demon-cli-win32-x64.zip`. Unzip anywhere and run `vst-demon-cli.exe` directly. No install, no PATH change, no admin. This is the artifact to embed in another application.

### Linux

- **`vst-demon-cli-linux-x64.tar.gz`** (x86_64). Extract with `tar xzf vst-demon-cli-linux-x64.tar.gz` — the tarball preserves the executable bit, so the extracted `vst-demon-cli` is ready to run. Move it onto your `PATH` if you like (e.g. `~/.local/bin/`).

`zenity` (or `kdialog`) must be present on `PATH` at runtime — it provides the Open/Save file dialogs. Install with your package manager (`sudo apt-get install zenity`) if missing.

See [Platform notes](#platform-notes) for the plugin-path requirement and keyboard shortcuts.

### macOS

- **`vst-demon-cli-darwin-arm64.tar.gz`** (Apple Silicon). Extract with `tar xzf vst-demon-cli-darwin-arm64.tar.gz`.

## Usage

```
vst-demon-cli --plugin <path.vst3> [--plugin-name <shell-sub-plugin>] [--preset <path.vstpreset>]
vst-demon-cli --plugin <path.vst3> --list
```

- `--plugin <path.vst3>` — the plugin to open (required).
- `--plugin-name <name>` — for shell plugins that expose several sub-plugins in one file (e.g. Waves WaveShell), the class name to open. Use `--list` to discover the names. Omit for single-plugin files.
- `--preset <path.vstpreset>` — the preset file to load at startup and auto-save to. If the file exists its state is restored; if not, it is created on the first save. The parent directory is created recursively if it does not exist, so you can point `--preset` at a not-yet-existing folder; if the directory genuinely cannot be created, `vst-demon-cli` fails fast with a clear message before opening the editor. Omit to open the plugin at its default state (auto-save stays dormant until you assign a path via **Save Preset As**).
- `--list` — print the plugin's audio-effect class names as a JSON array and exit, without opening a window. This is how you discover shell sub-plugin names.
- `--help` — print usage and exit.

Example — open OTT and auto-save to a preset (Windows):

```
vst-demon-cli --plugin "C:\Program Files\Common Files\VST3\OTT.vst3" --preset "%USERPROFILE%\Documents\my-ott.vstpreset"
```

Example — open a plugin on Linux (the plugin path is a VST3 bundle directory):

```
vst-demon-cli --plugin ~/.vst3/"Surge XT.vst3" --preset ~/presets/my-patch.vstpreset
```

Example — list the sub-plugins in a Waves shell:

```
vst-demon-cli --plugin "C:\Program Files\Common Files\VST3\WaveShell3-VST3 10.0_x64.vst3" --list
["REQ 2 Mono","REQ 2 Stereo","REQ 4 Mono","REQ 4 Stereo","REQ 6 Mono","REQ 6 Stereo"]
```

Listing and opening a shell's sub-plugins works for any vendor's shell, but Waves _state_ does not round-trip through a standard `.vstpreset` — see [Limitations](#limitations).

### Auto-save

No explicit save is needed. Once a preset path is set (via `--preset` or **Save Preset As**), the plugin's state is written to that file whenever a knob gesture completes, the plugin marks its state dirty, a 1-second dirty poll detects a change, or the window closes. Writes are atomic. The file you are looking at is always the file being written — opens (startup / Open) and writes are both announced on stdout.

### File actions

The **Open Preset** and **Save Preset As** actions open native file dialogs and retarget auto-save to the chosen file; **Close** exits after a final save. How you invoke them depends on the platform:

- **Windows / macOS** — a **File** menu: **Open Preset...**, **Save Preset As...**, **Close** (macOS shortcuts ⌘O / ⇧⌘S / ⌘W).
- **Linux** — there is no menu bar; the actions are keyboard shortcuts: **Ctrl+O** (Open Preset), **Ctrl+Shift+S** (Save Preset As), **Ctrl+W** (Close).

## stdout event contract

`vst-demon-cli` emits line-delimited JSON to stdout — one object per line, nothing else. All logging and diagnostics go to stderr, so stdout is a clean event stream a parent process can parse. The file on disk is the source of truth; the tool is fully functional with stdout ignored.

| Event                          | When                                                                                                                               |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| `{"event":"ready"}`            | the editor window is up                                                                                                            |
| `{"event":"open","path":...}`  | the auto-save target was opened: at startup when `--preset` is given (even if the file does not exist yet), and on **Open Preset** |
| `{"event":"saved","path":...}` | a write to the preset file completed (edit / poll / first create / close, and **Save Preset As**)                                  |
| `{"event":"closed"}`           | before a clean exit                                                                                                                |

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

## Limitations

- **Waves shell plugins.** Sub-plugins enumerate (`--list`) and open (`--plugin-name`) correctly, but their state does not round-trip through a standard `.vstpreset`: reopening a `vst-demon-cli`-written Waves preset resets the plugin to its default, because the Waves component's `setState` does not reconstruct edited state from the standard VST3 component chunk (the same limitation reproduces in Pedalboard). The container `vst-demon-cli` writes is structurally valid — this is a Waves property, not a writer defect. Author Waves presets in a Waves-aware host.

## Platform notes

### Linux

- **Plugin path is a bundle directory.** `--plugin` must point at a VST3 bundle directory, not a bare `.so`. A Linux VST3 has the layout `<Name>.vst3/Contents/x86_64-linux/<Name>.so`; pass the top-level `<Name>.vst3` directory. Pointing `--plugin` at a bare `.so` file produces a clear error (the module is not a bundle directory). Plugins normally live in `~/.vst3/`.
- **File dialogs need `zenity` (or `kdialog`).** The Open/Save dialogs are a `zenity` subprocess, falling back to `kdialog` if `zenity` is absent. One of them must be on `PATH`; if neither is found the dialog request prints a message to stderr and does nothing.
- **No menu bar.** The File actions are the keyboard shortcuts listed above (Ctrl+O / Ctrl+Shift+S / Ctrl+W).
- **WSLg.** On Windows 11, `vst-demon-cli` runs under WSLg (Windows' built-in WSL GUI support) — the plugin editor renders on the Windows desktop. Install the Linux build inside your WSL distribution and run it there.

### macOS (experimental)

- **Experimental.** The macOS binary is built and `--help`-smoke-tested in CI but has not been verified on Apple hardware. It may not work; reports are welcome.
- **Gatekeeper unblock.** The binary carries only an ad-hoc signature (`codesign -s -`), not a Developer ID, so Gatekeeper blocks it on first run as a downloaded, unidentified binary. To run it: **right-click** (or Control-click) the `vst-demon-cli` binary in Finder → **Open** → confirm in the dialog. From a terminal you can instead strip the quarantine attribute:

    ```
    xattr -d com.apple.quarantine vst-demon-cli
    ```

    This is the macOS analogue of the Windows SmartScreen unblock above.

## License

MIT. `vst-demon-cli` is an open utility built on Steinberg's MIT-licensed [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (v3.8.0). VST is a trademark of Steinberg Media Technologies GmbH.
