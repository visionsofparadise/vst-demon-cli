"""Round-trip check: does a vst-demon-authored preset change the plugin's output?

Loads the plugin twice into Pedalboard: once at default state, once with the given
.vstpreset applied via load_preset(). Processes 1 s of identical noise through each and
asserts the outputs differ. A preset that captured non-default state must audibly change
the output; if it does not, the writer failed to persist the plugin's state.

Usage:
    check_pedalboard_roundtrip.py --plugin <path.vst3> --preset <path.vstpreset> [--plugin-name <name>]

Run with the vst-host venv python (pedalboard 0.9.22 + numpy):
    C:\\Users\\mttcv\\Projects\\Code\\vst-host\\.venv\\Scripts\\python.exe

Exit 0 when outputs differ (round-trip works), nonzero otherwise.
"""
import argparse
import sys

import numpy as np
import pedalboard

SAMPLE_RATE = 48000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--preset", required=True)
    ap.add_argument("--plugin-name", default=None)
    args = ap.parse_args()

    np.random.seed(42)
    audio = (np.random.randn(int(SAMPLE_RATE * 1.0), 2) * 0.1).astype(np.float32)

    def load():
        if args.plugin_name:
            return pedalboard.load_plugin(args.plugin, plugin_name=args.plugin_name)
        return pedalboard.load_plugin(args.plugin)

    default_plugin = load()
    out_default = default_plugin.process(audio, sample_rate=SAMPLE_RATE, reset=True)

    preset_plugin = load()
    preset_plugin.load_preset(args.preset)
    out_preset = preset_plugin.process(audio, sample_rate=SAMPLE_RATE, reset=True)

    diff = np.abs(out_default - out_preset)
    max_diff = float(diff.max())
    rms_diff = float(np.sqrt((diff ** 2).mean()))

    print(f"plugin : {args.plugin}"
          + (f" [{args.plugin_name}]" if args.plugin_name else ""))
    print(f"preset : {args.preset}")
    print(f"audio default vs preset: max={max_diff:.6e}  rms={rms_diff:.6e}")

    if max_diff == 0.0:
        print("FAIL: preset produces identical output to default state — "
              "the writer did not capture non-default state", file=sys.stderr)
        sys.exit(1)

    print("OK: preset output differs from default (round-trip works)")
    sys.exit(0)


if __name__ == "__main__":
    main()
