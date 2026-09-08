#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# F-033's third clause: switching display modes must not interrupt audio or
# drop a frame.
#
# `measure-frames.sh` cannot answer this and never could. It sweeps the modes
# one at a time and measures each in steady state, settling the GPU between
# them — which is the right way to ask what a mode *costs* and the wrong way to
# ask what *changing* one costs. The event this clause is about happens in the
# gap that tool leaves between its measurements.
#
# **It measures with the swap interval left on, which is the opposite of what
# AV-002 does.** A dropped frame only exists where something is pacing them:
# with vsync off the loop free-runs and a missed frame is indistinguishable
# from a fast one. With vsync on, an interval over 1.5x the budget is one frame
# missed and nothing else, and that is what `FrameTimer` counts as late.
#
# Two runs of equal length, one steady and one switching every 400 ms, so the
# switching run's late count has something to be judged against. A machine that
# drops frames while doing nothing is not evidence about switching, and the
# control is what tells those apart.
#
#   usage: tools/verify-mode-switch.sh [seconds] [path-to-ferrolux]
#          default: 20 seconds per run, build-release/ferrolux

set -u

RUN_SECONDS="${1:-20}"
PLAYER="${2:-build-release/ferrolux}"
[ -x "$PLAYER" ] || { echo "no player at $PLAYER" >&2; exit 2; }
PLAYER=$(readlink -f "$PLAYER")

[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || {
    echo "needs a display; a dropped frame needs something to drop it from" >&2; exit 2; }
command -v gst-launch-1.0 >/dev/null || { echo "needs gst-launch-1.0" >&2; exit 2; }
pgrep -x ferrolux >/dev/null && {
    echo "a ferrolux is already running; this needs the instance it starts itself" >&2
    exit 2
}

WORK=$(mktemp -d)
export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"
mkdir -p "$XDG_CONFIG_HOME/ferrolux" "$XDG_DATA_HOME"

failures=0
cleanup() {
    pkill -x ferrolux 2>/dev/null
    sleep 1
    if [ "${failures:-0}" -eq 0 ]; then rm -rf "$WORK"; else printf 'logs kept in %s\n' "$WORK"; fi
}
trap cleanup EXIT INT TERM

check() {
    if [ "$1" -eq 0 ]; then
        printf '  [pass] %s%s\n' "$2" "${3:+ — $3}"
    else
        printf '  [FAIL] %s%s\n' "$2" "${3:+ — $3}"
        failures=$((failures + 1))
    fi
}

# Audible playback is the other half of the clause, and it has to be real
# playback: the meters are fed from the audio path, so a silent run switches
# between modes that all have nothing to draw.
cat > "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" <<CONF
[playback]
volume=0
CONF
TONE="$WORK/tone.flac"
gst-launch-1.0 -q audiotestsrc num-buffers=4000 wave=6 freq=200 volume=0.5 \
    ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
    ! flacenc ! filesink location="$TONE" >/dev/null 2>&1

run() {  # run <label> <switch interval ms, 0 for none>
    local label="$1"
    local switch="$2"
    LOG="$WORK/$label.log"

    FERROLUX_FRAME_MEASURE="$RUN_SECONDS" \
    FERROLUX_FRAME_MEASURE_VSYNC=1 \
    FERROLUX_MODE_SWITCH="$switch" \
    GST_DEBUG="pulsesink:2" \
        "$PLAYER" --play "$TONE" >"$LOG" 2>&1
    STATUS=$?

    FRAMES_LINE=$(grep -m1 '^FRAMES ' "$LOG")
    FRAMES=$(sed -n 's/.*[^_]frames=\([0-9]*\).*/\1/p' <<<"$FRAMES_LINE")
    LATE=$(sed -n 's/.*late=\([0-9]*\).*/\1/p' <<<"$FRAMES_LINE")
    WORST=$(sed -n 's/.*interval_worst=\([0-9.]*\)ms.*/\1/p' <<<"$FRAMES_LINE")
    MEAN=$(sed -n 's/.*interval_mean=\([0-9.]*\)ms.*/\1/p' <<<"$FRAMES_LINE")
    UNDERRUNS=$(grep -c "Got underflow" "$LOG")
    SWITCH_LINE=$(grep -m1 '^SWITCH ' "$LOG")
    SWITCHES=$(sed -n 's/.*switches=\([0-9]*\).*/\1/p' <<<"$SWITCH_LINE")
    IN_WINDOWS=$(sed -n 's/.*late_in_windows=\([0-9]*\).*/\1/p' <<<"$SWITCH_LINE")
    WINDOW_MS=$(sed -n 's/.*window_ms=\([0-9]*\).*/\1/p' <<<"$SWITCH_LINE")
}

printf '\n== steady, one mode, %ss ==\n' "$RUN_SECONDS"
run steady 0
printf '  %s\n  underruns=%s\n' "${FRAMES_LINE:-no FRAMES line}" "$UNDERRUNS"
STEADY_LATE="${LATE:-0}"; STEADY_WORST="${WORST:-0}"; STEADY_FRAMES="${FRAMES:-0}"
STEADY_MEAN="${MEAN:-0}"; STEADY_UNDER="$UNDERRUNS"

printf '\n== switching every 400 ms, %ss ==\n' "$RUN_SECONDS"
run switching 400
printf '  %s\n  %s\n  underruns=%s\n' \
    "${FRAMES_LINE:-no FRAMES line}" "${SWITCH_LINE:-no SWITCH line}" "$UNDERRUNS"
SWITCH_LATE="${LATE:-0}"; SWITCH_WORST="${WORST:-0}"; SWITCH_FRAMES="${FRAMES:-0}"
SWITCH_MEAN="${MEAN:-0}"; SWITCH_UNDER="$UNDERRUNS"

printf '\nF-033 — switching modes drops no frame and interrupts no audio\n'

# A run that rendered nothing, or one where vsync was not actually honoured,
# would report a flawless zero. Both are refused before anything is concluded.
check "$([ "${SWITCH_FRAMES:-0}" -gt 100 ] && echo 0 || echo 1)" \
      "the switching run rendered" "$SWITCH_FRAMES frames over ${RUN_SECONDS}s"
check "$(awk -v m="$SWITCH_MEAN" 'BEGIN { exit !(m > 8 && m < 40) }' && echo 0 || echo 1)" \
      "and was paced by the display, so a dropped frame is a thing that can happen" \
      "mean interval ${SWITCH_MEAN} ms — free-running would be far below this"

check "$([ "${SWITCHES:-0}" -ge 10 ] && echo 0 || echo 1)" \
      "and it actually switched" "${SWITCHES:-0} mode changes"

# **The check the clause is about.** Not how many frames the run dropped — this
# machine drops one or two in twenty seconds while doing nothing, because it
# shares a GPU with a compositor — but whether any of them can be laid at a mode
# change's door. A frame lost to a switch appears within a frame or two of it;
# one lost to the compositor appears anywhere.
check "$([ "${IN_WINDOWS:-1}" -eq 0 ] && echo 0 || echo 1)" \
      "no dropped frame followed a switch" \
      "${IN_WINDOWS:-?} in the ${WINDOW_MS:-?} ms after any of ${SWITCHES:-?} switches"

# What the run dropped in total, and what the steady run dropped over the same
# stretch, are reported rather than asserted. Requiring zero would be requiring
# an idle machine, which is not what the clause says and not a state a user is
# ever in.
printf '  [note] %s frames dropped over the whole switching run, %s over the steady one; the check above is what says whether either belongs to a switch\n' \
    "$SWITCH_LATE" "$STEADY_LATE"
check "$(awk -v a="$STEADY_WORST" -v b="$SWITCH_WORST" 'BEGIN { exit !(b < a * 1.5 + 4) }' && echo 0 || echo 1)" \
      "and switching does not lengthen the worst frame the run saw" \
      "${SWITCH_WORST} ms switching against ${STEADY_WORST} ms steady"

check "$([ "${SWITCH_UNDER:-1}" -eq 0 ] && echo 0 || echo 1)" \
      "and the sink never ran dry while the modes changed under it" \
      "${SWITCH_UNDER} underruns, ${STEADY_UNDER} steady"

printf '\n'
if [ "$failures" -eq 0 ]; then
    echo "PASS — modes switch without dropping a frame or interrupting audio"
else
    echo "FAILED ($failures)"
fi
exit $((failures > 0))
