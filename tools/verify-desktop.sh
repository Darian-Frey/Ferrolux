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
setprop() { gdbus call $D --method org.freedesktop.DBus.Properties.Set \
                org.mpris.MediaPlayer2.Player "$1" "<$2>" >/dev/null 2>&1; }

# Settings are doubles written through a text file and read back over D-Bus, so
# nothing here compares them for equality.
near() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a - b < 0.001 && b - a < 0.001) }'; }
ini() { sed -n "s/^$1=//p" "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" | tail -1; }

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
call Play; sleep 2

# BUG-029. Playback carries on between the reading taken here and the quit
# landing, so where it is when that reading is taken decides whether this
# section is a test or a coin toss. Left to run on from wherever the earlier
# checks finished, it read 9.12 s of a nine-second track — on the boundary,
# every run — and a gapless handover in that window moves the session on to the
# next track at position zero. The player is right to save that; the reading
# taken four seconds earlier is what is stale.
#
# So playback is placed rather than found. Two seconds into a nine-second track
# leaves the whole window clear of the boundary, and the quit still happens
# while playing, which is the case F-015 is actually about — pausing first would
# make this deterministic by no longer testing it.
trackid=$(prop Metadata | grep -o "'mpris:trackid': <objectpath '[^']*'>" \
          | sed "s/.*objectpath '\([^']*\)'.*/\1/")
gdbus call $D --method org.mpris.MediaPlayer2.Player.SetPosition \
    "objectpath '$trackid'" 2000000 >/dev/null 2>&1
sleep 1

saved_pos=$(prop Position | sed 's/^int64 //')
# The placement is asserted rather than assumed. If `SetPosition` were ever to
# stop working, everything below would still pass while measuring the race this
# exists to remove.
[ -n "$saved_pos" ] && [ "$saved_pos" -ge 1500000 ] && [ "$saved_pos" -lt 5000000 ]
check $? "playback can be placed mid-track, clear of any handover" \
      "asked for 2 s, reading ${saved_pos} µs"

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

# ---- F-004 ------------------------------------------------------------------
# The clause is "both persist across restart", and it had been verified by hand
# in Phase 1 and by nothing since. The acceptance harness cannot cover it: it
# runs headless and exits without going through `aboutToQuit`, which is where
# settings are written, so it can only ever observe a file nobody saved.
#
# **What makes this a check on the player rather than on a file is the direction
# `Settings::save()` reads in.** It does not copy the settings file forward; it
# asks the engine for its current volume and balance and writes those. So a
# distinctive value that is still in the file after a launch and a clean quit has
# been through the engine twice — restored into it on start, read back out of it
# on exit. A player that ignored the file on startup would overwrite it with the
# defaults on the way out, and the seeded values are chosen to be nothing like
# the defaults so that such a failure cannot look like a pass.
echo
echo "Volume and balance persist (F-004)"

stop
cat > "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" <<SEED
[playback]
volume=0.42
balance=-0.35
SEED

start
check $? "it starts on a settings file holding non-default values" \
      "volume 0.42 against a default of 0.7, balance -0.35 against 0"

# Directly observed, rather than inferred from the file: MPRIS reports the
# engine's own taper position, so this is the value that is actually in effect.
volume=$(prop Volume)
near "$volume" 0.42
check $? "the volume it restored is the one in the file, read back off the engine" \
      "MPRIS reports $volume"

# Change it while running, so that what is checked next is a save and not the
# file that was already there.
setprop Volume 0.9
sleep 0.5
near "$(prop Volume)" 0.9
check $? "a volume set while running takes effect" "MPRIS reports $(prop Volume)"

stop
near "$(ini volume)" 0.9
check $? "and a clean quit writes it" "the file says $(ini volume)"

# Balance has no D-Bus surface — MPRIS has no such property — so it cannot be
# observed directly in a running player. It does not need to be: `save()` writes
# what the engine holds, so a balance that comes back out of the file unchanged
# is one the engine was carrying, which it can only have got from the file on
# startup.
near "$(ini balance)" -0.35
check $? "balance survives the same round trip, through the engine both ways" \
      "the file says $(ini balance)"

start
near "$(prop Volume)" 0.9
check $? "and the next launch comes up at the volume it was left at" \
      "MPRIS reports $(prop Volume)"
stop

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
