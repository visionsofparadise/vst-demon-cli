#!/usr/bin/env bash
# Integration test: open a real VST3 editor, auto-close through the normal close path, and assert
# the CLI contract — stdout event stream, exit code, and a written preset. Runs on all three CI
# legs against the SDK's `again` example plugin (built from the pinned vst3sdk submodule with
# SMTG_ENABLE_VST3_PLUGIN_EXAMPLES + SMTG_ENABLE_VSTGUI_SUPPORT); Linux needs an X server (xvfb-run).
#
# Usage: tests/integration.sh <vst-demon-binary> <plugin.vst3>
set -u

BIN=$1
PLUGIN=$2

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

# Run "$@" but kill it after $WATCHDOG_SECS if it hasn't exited (a backstop against a hung editor —
# --close-after-ms should close it long before). Portable across the three CI shells: GNU `timeout`
# is absent on the macOS runner, so this uses a plain background sleep+kill instead. Returns the
# command's exit code (non-zero — 137 from SIGKILL — if the watchdog fired).
WATCHDOG_SECS=120
run_watchdogged() {
	"$@" &
	local pid=$!
	( sleep "$WATCHDOG_SECS"; kill -9 "$pid" 2> /dev/null ) &
	local dog=$!
	local code=0
	wait "$pid" 2> /dev/null || code=$?
	kill "$dog" 2> /dev/null
	wait "$dog" 2> /dev/null || true
	return $code
}

[ -e "$BIN" ] || fail "binary not found: $BIN"
[ -e "$PLUGIN" ] || fail "plugin not found: $PLUGIN"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
PRESET="$WORK/integration.vstpreset"
EVENTS="$WORK/events.log"
ERRORS="$WORK/stderr.log"

"$BIN" --help > /dev/null 2>&1 || fail "--help exited $?"

LIST=$("$BIN" --plugin "$PLUGIN" --list 2> "$ERRORS") || { cat "$ERRORS" >&2; fail "--list exited nonzero"; }
case "$LIST" in
	'["'*'"]') ;;
	*) fail "--list output is not a JSON string array: $LIST" ;;
esac
NAME=${LIST#'["'}
NAME=${NAME%%'"'*}
echo "--list ok: $LIST (opening \"$NAME\")"

# Two sessions: the first creates the preset from default state, the second loads it back — so both
# the create-on-first-save and the restore path run.
run_session() {
	local label=$1
	local code=0
	run_watchdogged "$BIN" --plugin "$PLUGIN" --plugin-name "$NAME" --preset "$PRESET" \
		--close-after-ms 5000 > "$EVENTS" 2> "$ERRORS" || code=$?
	if [ $code -ne 0 ]; then
		echo "--- stderr ---" >&2
		cat "$ERRORS" >&2
		echo "--- events ---" >&2
		cat "$EVENTS" >&2
		fail "$label: session exited $code"
	fi

	local sequence
	sequence=$(sed -n 's/.*"event":"\([a-z]*\)".*/\1/p' "$EVENTS" | tr '\n' ' ')
	case "$sequence" in
		'ready open '*'saved closed ') ;;
		*) fail "$label: bad event sequence: $sequence" ;;
	esac

	[ -s "$PRESET" ] || fail "$label: preset file missing or empty"
	local size
	size=$(wc -c < "$PRESET")
	[ "$size" -gt 48 ] || fail "$label: preset implausibly small ($size bytes)"
	echo "$label ok: events [$sequence], preset $size bytes"
}

run_session "session 1 (create)"
run_session "session 2 (reload)"

echo "PASS"
