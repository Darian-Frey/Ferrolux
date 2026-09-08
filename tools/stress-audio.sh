#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# AV-001 detection, the half that runs: does the streaming thread stay clear of
# application work when the machine is busy?
#
# AV-001 is Critical and its symptom is intermittent, which is what makes it
# expensive. Nothing crashes and no error is posted; audio simply drops out
# occasionally, on somebody else's machine, under a load the developer's machine
# never sees. So this reproduces the load and counts two things that a dropout
# needs both of to happen:
#
#   the callback     how long the application's own code holds a streaming
#                    thread, measured from inside it by `core/StreamTimer`.
#                    This is the part AV-001 is about and the part we control.
#   the underruns    how many times the sink ran out of audio to play, reported
#                    by `pulsesink` itself. This is the symptom, counted at the
#                    device, downstream of every element and of the scheduler.
#
# Both are needed. The callback time alone cannot prove there was no dropout —
# starvation can come from a thread that never touches our code. The underrun
# count alone cannot say whose fault a dropout was. Together they distinguish
# "we stalled the audio path" from "this machine cannot keep up", which are
# different bugs with different owners.
#
# **The load is synthetic and deliberate.** One spinning thread per core takes
# the CPU the streaming thread needs to be scheduled on, and a `dd` loop takes
# the I/O bandwidth a decoder reads through. AV-001 names both. A run on an idle
# machine measures nothing: every one of these numbers is fine when nothing else
# is happening, which is precisely why the vector survives to release.
#
# **It is hermetic and it is quiet.** `XDG_CONFIG_HOME` and `XDG_DATA_HOME` go
# into a temporary directory, so the settings and session belong to the run.
# Volume is set to zero: the audio path runs in full — buffers are decoded,
# converted and handed to the device on the same threads with the same timing —
# but nothing is heard, because this occupies the machine for a minute and
# should not also occupy the room. Muting is downstream of every thread this
# measures.
#
#   usage: tools/stress-audio.sh [seconds] [path-to-ferrolux]
#          default: 45 seconds, build-release/ferrolux

set -u

SECONDS_TO_RUN="${1:-45}"
PLAYER="${2:-build-release/ferrolux}"
[ -x "$PLAYER" ] || { echo "no player at $PLAYER" >&2; exit 2; }
PLAYER=$(readlink -f "$PLAYER")

command -v gst-launch-1.0 >/dev/null || { echo "needs gst-launch-1.0" >&2; exit 2; }

# F-052 means a second launch hands its arguments to the first and exits, so a
# player already running would leave this measuring a process it never started
# and reporting no STREAM line at all. Say so rather than producing that.
pgrep -x ferrolux >/dev/null && {
    echo "a ferrolux is already running; this needs the instance it starts itself" >&2
    exit 2
}

WORK=$(mktemp -d)
export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"
mkdir -p "$XDG_CONFIG_HOME/ferrolux" "$XDG_DATA_HOME"

LOAD_PIDS=()
cleanup() {
    for pid in ${LOAD_PIDS+"${LOAD_PIDS[@]}"}; do kill "$pid" 2>/dev/null; done
    pkill -x ferrolux 2>/dev/null
    sleep 1
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

failures=0
check() {  # check <ok:0|1> <what> [detail]
    if [ "$1" -eq 0 ]; then
        printf '  [pass] %s%s\n' "$2" "${3:+ — $3}"
    else
        printf '  [FAIL] %s%s\n' "$2" "${3:+ — $3}"
        failures=$((failures + 1))
    fi
}

# Silence, and a starting state that plays rather than waits to be told to.
cat > "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" <<CONF
[playback]
volume=0
balance=0
CONF

# ---- material ---------------------------------------------------------------
# Short files on purpose. `about-to-finish` fires once per track, near its end,
# and it is the only callback on a streaming thread — so the number of samples
# this run collects is the number of tracks it gets through. Three-second tracks
# turn a 45-second run into roughly fifteen handovers; three-minute tracks would
# turn it into none, and the report would be a page of zeroes that looked like a
# pass.
echo "generating material"
TRACKS=()
for i in $(seq 1 20); do
    f="$WORK/track-$i.flac"
    gst-launch-1.0 -q audiotestsrc num-buffers=130 wave=sine freq=$((220 + i * 40)) volume=0.3 \
        ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
        ! flacenc ! filesink location="$f" >/dev/null 2>&1
    TRACKS+=("$f")
done
echo "  20 tracks of about three seconds each"

# ---- the load ---------------------------------------------------------------
CORES=$(nproc)
start_load() {
    for _ in $(seq 1 "$CORES"); do
        ( while :; do :; done ) &
        LOAD_PIDS+=($!)
    done
    ( while :; do
        dd if=/dev/urandom of="$WORK/churn" bs=1M count=64 conv=fsync 2>/dev/null
        rm -f "$WORK/churn"
      done ) &
    LOAD_PIDS+=($!)
}
stop_load() {
    for pid in ${LOAD_PIDS+"${LOAD_PIDS[@]}"}; do kill "$pid" 2>/dev/null; done
    LOAD_PIDS=()
    wait 2>/dev/null
}

# ---- one run ----------------------------------------------------------------
# `pulsesink` logs "Got underflow" at WARNING when the device runs dry, so the
# underrun count comes from the sink rather than from anything of ours. Nothing
# is added to the audio path to obtain it, which matters: a probe installed to
# measure starvation would be one more piece of application work on the thread
# under suspicion.
run() {  # run <label>
    local label="$1"
    local log="$WORK/$label.log"
    FERROLUX_STREAM_MEASURE="$SECONDS_TO_RUN" \
    GST_DEBUG="pulsesink:2" \
        "$PLAYER" --play "${TRACKS[@]}" >"$log" 2>&1
    OWN_LINE=$(grep -m1 '^STREAM own ' "$log")
    HANDOVER_LINE=$(grep -m1 '^STREAM handover ' "$log")
    UNDERRUNS=$(grep -c "Got underflow" "$log")
    CALLS=$(sed -n 's/.*calls=\([0-9]*\).*/\1/p' <<<"$OWN_LINE")
    WORST=$(sed -n 's/.*worst=\([0-9.]*\)us.*/\1/p' <<<"$OWN_LINE")
    OVER=$(sed -n 's/.*over=\([0-9]*\).*/\1/p' <<<"$OWN_LINE")
    HANDOVER_WORST=$(sed -n 's/.*worst=\([0-9.]*\)us.*/\1/p' <<<"$HANDOVER_LINE")
    HANDOVER_MEAN=$(sed -n 's/.*mean=\([0-9.]*\)us.*/\1/p' <<<"$HANDOVER_LINE")
}

report() {
    printf '  %s\n  %s\n  underruns=%s\n' \
        "${OWN_LINE:-no STREAM own line}" "${HANDOVER_LINE:-no STREAM handover line}" "$UNDERRUNS"
}

printf '\n== baseline: an idle machine ==\n'
run baseline
report
BASE_WORST="${WORST:-0}"; BASE_CALLS="${CALLS:-0}"; BASE_UNDER="$UNDERRUNS"
BASE_HANDOVER="${HANDOVER_WORST:-0}"

printf '\n== under load: %s spinning threads and continuous disc I/O ==\n' "$CORES"
start_load
sleep 2
run loaded
stop_load
report
LOAD_WORST="${WORST:-0}"; LOAD_CALLS="${CALLS:-0}"; LOAD_OVER="${OVER:-0}"; LOAD_UNDER="$UNDERRUNS"
LOAD_HANDOVER="${HANDOVER_WORST:-0}"; LOAD_HANDOVER_MEAN="${HANDOVER_MEAN:-0}"

# ---- the findings -----------------------------------------------------------
printf '\nAV-001 — application work on the streaming thread\n'

# A run that never reached a handover has measured nothing, and would otherwise
# report a flawless streaming thread on the strength of never having used one.
check "$([ "${BASE_CALLS:-0}" -gt 0 ] && echo 0 || echo 1)" \
      "the idle run exercised the streaming-thread callback" \
      "$BASE_CALLS handovers"
check "$([ "${LOAD_CALLS:-0}" -gt 0 ] && echo 0 || echo 1)" \
      "and so did the loaded run" \
      "$LOAD_CALLS handovers"

check "$(awk -v w="$LOAD_WORST" 'BEGIN { exit !(w < 1000) }' && echo 0 || echo 1)" \
      "under load, the application's own work never holds a streaming thread for 1 ms" \
      "worst ${LOAD_WORST}us, budget 1000us"
check "$([ "${LOAD_OVER:-0}" -eq 0 ] && echo 0 || echo 1)" \
      "and no single call of it went over budget" \
      "$LOAD_OVER of $LOAD_CALLS"

# The handover is `playbin3`'s to spend and takes milliseconds, so it is not
# held to the budget above — it is held to the only thing that matters about it,
# which is whether the device noticed. Reported so that the number is on the
# record rather than absent, because a cost nobody prints is a cost nobody
# revisits when it doubles.
printf '  [note] the handover itself costs %sus at worst, %sus mean — GStreamer'"'"'s\n' \
    "$LOAD_HANDOVER" "$LOAD_HANDOVER_MEAN"

# The comparison is the point of running twice. Our callback takes a mutex the
# main thread also takes, so load *can* reach it — if the loaded figure is an
# order of magnitude worse than the idle one, that is contention rather than
# noise, and it is ours.
check "$(awk -v a="$BASE_WORST" -v b="$LOAD_WORST" 'BEGIN { exit !(b < 10 * (a + 1)) }' && echo 0 || echo 1)" \
      "load does not change what our own work costs" \
      "${BASE_WORST}us idle against ${LOAD_WORST}us loaded"
printf '  [note] the handover went %sus idle to %sus loaded\n' "$BASE_HANDOVER" "$LOAD_HANDOVER"

check "$([ "$LOAD_UNDER" -eq 0 ] && echo 0 || echo 1)" \
      "the sink never ran dry under load" \
      "$LOAD_UNDER underruns, $BASE_UNDER idle"

printf '\n'
if [ "$failures" -eq 0 ]; then
    echo "PASS — the streaming thread does no application work, and the device never starved"
else
    echo "FAILED ($failures)"
fi
printf 'note: underruns are counted by pulsesink itself, downstream of every\n'
printf '      element. A dropout with a clean callback time is not this vector —\n'
printf '      it is the machine failing to schedule, and belongs to whoever set\n'
printf '      the buffer size. The two numbers are reported together so that\n'
printf '      distinction can be made rather than assumed.\n'
exit $((failures > 0))
