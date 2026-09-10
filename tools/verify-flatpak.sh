#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# Phase 7's acceptance, for the Flatpak: does a clean install play music?
#
# Unlike `verify-package.sh`, this one really installs — into the user's own
# Flatpak repository, as `org.ferrolux.Ferrolux`, replacing whatever was there.
# There is no way to run a Flatpak from an extracted directory the way a `.deb`
# can be, and the sandbox is the thing being tested: its bus permissions, its
# renamed desktop entry, its access to the music directory, and a Qt six minor
# versions newer than the one everything else was measured on.
#
# The first build of this manifest found three defects, each caught by the
# export rather than by anything of ours: an icon that gdk-pixbuf could not
# recognise (BUG-033 — it had never loaded on any GTK desktop either), a desktop
# entry whose `Icon=` line pointed at a file the rename had removed, and a
# settings-daemon bus name the sandbox was not allowed to talk to, so media keys
# silently took the X11 grab path. All three are checked below.
#
#   usage: tools/verify-flatpak.sh          (builds, installs, runs, checks)

set -u

command -v flatpak-builder >/dev/null || {
    echo "needs flatpak-builder: sudo apt install flatpak-builder" >&2; exit 2; }
flatpak info org.kde.Sdk//6.10 >/dev/null 2>&1 || {
    echo "needs the runtime: flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10" >&2; exit 2; }
for tool in gdbus gst-launch-1.0 python3; do
    command -v "$tool" >/dev/null || { echo "needs $tool" >&2; exit 2; }
done
pgrep -x ferrolux >/dev/null && {
    echo "a ferrolux is already running; this needs the instance it starts itself" >&2
    exit 2
}

WORK=$(mktemp -d)
MUSIC="$HOME/Music/.ferrolux-flatpak-verify"
failures=0
cleanup() {
    gdbus call --session --dest org.mpris.MediaPlayer2.ferrolux \
        --object-path /org/mpris/MediaPlayer2 --method org.mpris.MediaPlayer2.Quit >/dev/null 2>&1
    sleep 1
    pkill -x ferrolux 2>/dev/null
    rm -rf "$MUSIC"
    if [ "$failures" -eq 0 ]; then rm -rf "$WORK"; else printf 'kept: %s\n' "$WORK"; fi
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

echo
echo "Building and installing"
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/org.ferrolux.Ferrolux.yml >"$WORK/build.log" 2>&1
check $? "flatpak-builder builds and installs" "$(grep -c 'warning:' "$WORK/build.log") compiler warnings"
grep -q "error" "$WORK/build.log" && grep -i "error" "$WORK/build.log" | head -3

echo
echo "What the export accepted"
EXPORT=build-flatpak/export/share
[ -f "$EXPORT/applications/org.ferrolux.Ferrolux.desktop" ]
check $? "the desktop entry is named for the app id"
grep -q "^Icon=org.ferrolux.Ferrolux$" "$EXPORT/applications/org.ferrolux.Ferrolux.desktop" 2>/dev/null
check $? "and its Icon= line was rewritten to match" \
      "$(grep '^Icon=' "$EXPORT/applications/org.ferrolux.Ferrolux.desktop" 2>/dev/null)"
[ -f "$EXPORT/icons/hicolor/scalable/apps/org.ferrolux.Ferrolux.svg" ]
check $? "the icon is named for the app id"

# BUG-033. gdk-pixbuf finds "<svg" near the start of a file or not at all, and
# the export uses it to decide whether the icon is an icon.
python3 - "$EXPORT/icons/hicolor/scalable/apps/org.ferrolux.Ferrolux.svg" <<'PY'
import sys
import gi; gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf
try:
    p = GdkPixbuf.Pixbuf.new_from_file(sys.argv[1])
    print(f"{p.get_width()}x{p.get_height()}")
except Exception as e:
    print(f"unreadable: {e}"); sys.exit(1)
PY
check $? "and gdk-pixbuf can read it, which is what every GTK launcher uses (BUG-033)"

echo
echo "Whether it runs in the sandbox"
mkdir -p "$HOME/.var/app/org.ferrolux.Ferrolux/config/ferrolux"
printf '[playback]\nvolume=0\n' > "$HOME/.var/app/org.ferrolux.Ferrolux/config/ferrolux/ferrolux.ini"
mkdir -p "$MUSIC"
gst-launch-1.0 -q audiotestsrc num-buffers=1200 wave=0 freq=440 volume=0.3 \
    ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
    ! flacenc ! filesink location="$MUSIC/tone.flac" >/dev/null 2>&1

flatpak run org.ferrolux.Ferrolux --play "$MUSIC/tone.flac" >"$WORK/player.log" 2>&1 &
D="--session --dest org.mpris.MediaPlayer2.ferrolux --object-path /org/mpris/MediaPlayer2"
prop() { gdbus call $D --method org.freedesktop.DBus.Properties.Get "$1" "$2" 2>/dev/null | sed 's/^(<//; s/>,)$//'; }

for _ in $(seq 1 40); do [ -n "$(prop org.mpris.MediaPlayer2.Player PlaybackStatus)" ] && break; sleep 0.5; done
[ -n "$(prop org.mpris.MediaPlayer2.Player PlaybackStatus)" ]
check $? "the sandboxed player starts and answers on the bus" \
      "the manifest's --own-name for MPRIS is what allows this"

[ "$(prop org.mpris.MediaPlayer2.Player PlaybackStatus)" = "'Playing'" ]
check $? "and plays a file from the music directory" "$(prop org.mpris.MediaPlayer2.Player PlaybackStatus)"

first=$(prop org.mpris.MediaPlayer2.Player Position | sed 's/^int64 //'); sleep 2
second=$(prop org.mpris.MediaPlayer2.Player Position | sed 's/^int64 //')
[ -n "$first" ] && [ -n "$second" ] && [ "$second" -gt "$first" ]
check $? "with the position advancing, so the runtime's GStreamer is decoding it" \
      "$((first / 1000)) ms to $((second / 1000)) ms"

[ "$(prop org.mpris.MediaPlayer2 DesktopEntry)" = "'org.ferrolux.Ferrolux'" ]
check $? "MPRIS reports the app id as its desktop entry, not the host name" \
      "$(prop org.mpris.MediaPlayer2 DesktopEntry)"

# Only meaningful on a desktop that has a daemon; on one that does not, the
# grab path is the right answer and this is skipped rather than failed.
if gdbus call --session --dest org.gnome.SettingsDaemon --object-path / \
       --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1 \
   || gdbus call --session --dest org.gnome.SettingsDaemon.MediaKeys --object-path / \
       --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
    sleep 1
    grep -q "SettingsDaemon answers" "$WORK/player.log"
    check $? "media keys went through the settings daemon rather than an X11 grab" \
          "$(grep -m1 'media keys:' "$WORK/player.log")"
else
    printf '  [skip] no settings daemon on this desktop; the grab path is correct here\n'
fi

gdbus call $D --method org.mpris.MediaPlayer2.Quit >/dev/null 2>&1
sleep 2
! pgrep -x ferrolux >/dev/null
check $? "and quits when asked"

printf '\n'
if [ "$failures" -eq 0 ]; then
    echo "PASS — the Flatpak builds, exports what a launcher needs, and plays music in its sandbox"
else
    echo "FAILED ($failures)"
fi
printf 'note: this installs org.ferrolux.Ferrolux into the user Flatpak repository\n'
printf '      and leaves it there. Musepack is the one F-001 format the runtime\n'
printf '      does not decode; see the manifest.\n'
exit $((failures > 0))
