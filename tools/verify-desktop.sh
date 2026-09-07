#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# IMP-010 detection: does platform/ actually behave on a desktop?
#
# Six of the seven classes in platform/ cannot be unit tested and should not
# pretend to be. An MPRIS service needs a session bus, media keys need a
# settings daemon, and a single-instance hand-off needs two processes — so this
# is a tool rather than a suite, for the same reason AV-002 and AV-005 are. A
# check that cannot run on the build machine is not a test; it is a script that
# fails and gets ignored.
#
# **It is hermetic.** `XDG_CONFIG_HOME` and `XDG_DATA_HOME` point into a
# temporary directory, so the settings this writes, the session it saves and the
# playlist it leaves behind are its own. Nothing here can disturb the settings of
# whoever is running it — which earlier rounds of this checking did, by hand, and
# had to put back afterwards.
#
#   usage: tools/verify-desktop.sh [path-to-ferrolux]     (default: build-release/ferrolux)

set -u

PLAYER="${1:-build-release/ferrolux}"
[ -x "$PLAYER" ] || { echo "no player at $PLAYER" >&2; exit 2; }
PLAYER=$(readlink -f "$PLAYER")

for tool in gdbus gst-launch-1.0; do
    command -v "$tool" >/dev/null || { echo "needs $tool" >&2; exit 2; }
done
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1 \
    || { echo "no session bus; this tool has nothing to measure" >&2; exit 2; }

WORK=$(mktemp -d)
export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"

failures=0
cleanup() {
    pkill -x ferrolux 2>/dev/null
    sleep 1
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

check() {  # check <ok:0|1> <what> [detail]
    if [ "$1" -eq 0 ]; then
        printf '  [pass] %s%s\n' "$2" "${3:+ — $3}"
    else
        printf '  [FAIL] %s%s\n' "$2" "${3:+ — $3}"
        failures=$((failures + 1))
    fi
}

D="--session --dest org.mpris.MediaPlayer2.ferrolux --object-path /org/mpris/MediaPlayer2"
prop() { gdbus call $D --method org.freedesktop.DBus.Properties.Get \
             org.mpris.MediaPlayer2.Player "$1" 2>/dev/null | sed 's/^(<//; s/>,)$//'; }
call() { gdbus call $D --method "org.mpris.MediaPlayer2.Player.$1" ${2:+"$2"} >/dev/null 2>&1; }

start() {  # start [args...]; waits until the player answers on the bus
    "$PLAYER" "$@" >"$WORK/player.log" 2>&1 &
    for _ in $(seq 1 40); do
        [ -n "$(prop PlaybackStatus)" ] && return 0
        sleep 0.5
    done
    return 1
}

# **Asked to quit, never killed.** SIGTERM does not reach `aboutToQuit`, so a
# `pkill` skips every save: the settings, the session and the play order are all
# written from that signal. Anything checking persistence that kills the process
# is checking that nothing was saved, and will say so in a way that looks like a
# defect in the application.
#
# MPRIS's own `Quit` is used rather than closing the window, so this tool needs
# no display — and it exercises that method into the bargain. `pkill` survives
# only in `cleanup`, for a player that has stopped answering.
stop() {
    gdbus call $D --method org.mpris.MediaPlayer2.Quit >/dev/null 2>&1
    for _ in $(seq 1 20); do
        pgrep -x ferrolux >/dev/null || { sleep 1; return 0; }
        sleep 0.5
    done
    pkill -x ferrolux 2>/dev/null
    sleep 1
    return 1
}

# ---- fixtures --------------------------------------------------------------
# Synthetic and generated here, so the tool needs no music library and two runs
# are comparable.
mkdir -p "$WORK/audio"
for n in 1 2 3; do
    gst-launch-1.0 -q audiotestsrc num-buffers=400 freq=$((220 * n)) \
        ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
        ! flacenc ! filesink location="$WORK/audio/track$n.flac" 2>/dev/null
done
echo "verify-desktop — platform/ against a real session"
echo "  settings and session under $WORK"
echo

# ---- MPRIS2, F-050 ---------------------------------------------------------
echo "MPRIS2 (F-050)"
start "$WORK/audio/track1.flac" "$WORK/audio/track2.flac" "$WORK/audio/track3.flac"
check $? "the player starts and answers on the bus"

gdbus introspect --session --dest org.mpris.MediaPlayer2.ferrolux \
    --object-path /org/mpris/MediaPlayer2 >"$WORK/introspect" 2>&1
grep -q "interface org.mpris.MediaPlayer2 " "$WORK/introspect"; \
    check $? "org.mpris.MediaPlayer2 is exported"
grep -q "interface org.mpris.MediaPlayer2.Player" "$WORK/introspect"; \
    check $? "org.mpris.MediaPlayer2.Player is exported"

[ "$(prop PlaybackStatus)" = "'Paused'" ]
check $? "a launch with paths does not start playing" "$(prop PlaybackStatus)"

gdbus monitor --session --dest org.mpris.MediaPlayer2.ferrolux >"$WORK/signals" 2>&1 &
MON=$!
sleep 1
call Play; sleep 3
[ "$(prop PlaybackStatus)" = "'Playing'" ]; check $? "Play plays"
call Next; sleep 3
[ "$(prop PlaybackStatus)" = "'Playing'" ]; check $? "Next keeps playing"
call PlayPause; sleep 2
[ "$(prop PlaybackStatus)" = "'Paused'" ]; check $? "PlayPause pauses"
call Stop; sleep 2
[ "$(prop PlaybackStatus)" = "'Stopped'" ]; check $? "Stop stops"
[ "$(prop Position)" = "int64 0" ]; check $? "and returns the position to zero" "$(prop Position)"
kill $MON 2>/dev/null

# Qt emits none of these by itself; every one is built by hand, so a service
# that polls correctly and never notifies is the failure this looks for.
grep -q "PropertiesChanged.*PlaybackStatus" "$WORK/signals"
check $? "PropertiesChanged is emitted for PlaybackStatus"
grep -q "PropertiesChanged.*Metadata" "$WORK/signals"
check $? "and for Metadata"

# ---- single instance, F-052 ------------------------------------------------
echo
echo "Single instance and the command line (F-052)"
before=$(pgrep -cx ferrolux)
"$PLAYER" "$WORK/audio/track1.flac" >/dev/null 2>&1
handoff=$?
sleep 2
check $([ "$handoff" -eq 0 ] && echo 0 || echo 1) "a second launch exits 0" "status $handoff"
[ "$(pgrep -cx ferrolux)" = "$before" ]
check $? "and does not start a second player" "$(pgrep -cx ferrolux) running"

"$PLAYER" --play "$WORK/audio/track2.flac" >/dev/null 2>&1; sleep 3
[ "$(prop PlaybackStatus)" = "'Playing'" ]; check $? "--play starts the file it is given"

"$PLAYER" --enqueue "$WORK/audio/track3.flac" >/dev/null 2>&1; sleep 2
[ "$(prop PlaybackStatus)" = "'Playing'" ]
check $? "--enqueue does not disturb what is playing"

# ---- session restore, F-015 ------------------------------------------------
echo
echo "Session restore (F-015)"
call Play; sleep 4
saved_pos=$(prop Position | sed 's/^int64 //')
saved_track=$(prop Metadata | grep -o "'xesam:url': <[^>]*>" | head -1)
stop
check $? "the player quits when asked, rather than being killed"

grep -q "^playlist=" "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini"
check $? "a session playlist path is recorded"
[ -f "$XDG_DATA_HOME/ferrolux/session.m3u8" ]
check $? "and the playlist itself is written"

start
check $? "it starts again with no arguments"
[ "$(prop PlaybackStatus)" = "'Paused'" ]
check $? "and does not resume playing on its own" "$(prop PlaybackStatus)"
[ "$(prop Metadata | grep -o "'xesam:url': <[^>]*>" | head -1)" = "$saved_track" ]
check $? "the track that was current comes back"
restored=$(prop Position | sed 's/^int64 //')
# At least where it was, and not wildly past the track: playback carries on
# between reading the figure and the quit landing, so the restored position is
# expected to be a little later than the one observed. Zero would mean the seek
# never happened, and a figure far beyond the track would mean it went to the
# wrong place.
[ -n "$restored" ] && [ "$restored" -ge "$saved_pos" ] \
    && [ "$restored" -lt $((saved_pos + 30000000)) ] 2>/dev/null
check $? "and the position with it, at or just past where it was" \
      "observed ${saved_pos} µs before quitting, restored ${restored} µs"

# ---- settings round trip ---------------------------------------------------
echo
echo "Settings (SPEC.md §Settings)"
cp "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" "$WORK/before.ini"
stop
start
check $? "it starts on an existing settings file"
stop
diff -q "$WORK/before.ini" "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" >/dev/null
check $? "every key survives a launch and a close unchanged" \
      "$(diff "$WORK/before.ini" "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" | head -4 | tr '\n' ' ')"

printf '\n%s (%d failure%s)\n' \
    "$([ $failures -eq 0 ] && echo PASSED || echo FAILED)" \
    "$failures" "$([ $failures -eq 1 ] || echo s)"
[ $failures -eq 0 ]
