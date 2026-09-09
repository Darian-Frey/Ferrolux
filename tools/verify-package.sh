#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# Phase 7's acceptance, for the `.deb`: does a clean install play music?
#
# **It installs nowhere.** The package is extracted into a temporary root and
# the binary is run from there, so this can be run on the development machine
# without `dpkg -i` and without leaving anything behind. What that costs is the
# dependency check — a real install would refuse if `gstreamer1.0-plugins-good`
# were missing, and this would not notice, because the machine building the
# package has everything. What it buys is the check that actually catches
# things: BUG-023, where the application ran perfectly from its build directory
# and showed no window anywhere else, for five phases, because every test ran
# the binary where it was built.
#
# The dependency list is inspected rather than resolved, which is the honest
# half-measure available here: the plugin sets GStreamer loads at run time are
# named in `CMakeLists.txt` by hand, because `dpkg-shlibdeps` reads the binary
# and nothing in the binary mentions a plugin it opens through the registry. A
# package missing them installs, launches, draws a panel and plays nothing.
#
#   usage: tools/verify-package.sh [build directory]     (default: build-release)

set -u

BUILD="${1:-build-release}"
[ -d "$BUILD" ] || { echo "no build directory at $BUILD" >&2; exit 2; }

for tool in cpack dpkg-deb gdbus gst-launch-1.0; do
    command -v "$tool" >/dev/null || { echo "needs $tool" >&2; exit 2; }
done
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1 \
    || { echo "no session bus; the player is asked to quit over MPRIS" >&2; exit 2; }
pgrep -x ferrolux >/dev/null && {
    echo "a ferrolux is already running; this needs the instance it starts itself" >&2
    exit 2
}

WORK=$(mktemp -d)
failures=0
cleanup() {
    pkill -x ferrolux 2>/dev/null
    sleep 1
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
echo "Building the package"
( cd "$BUILD" && cpack -G DEB ) >"$WORK/cpack.log" 2>&1
check $? "cpack builds a .deb" "$(grep -o 'package: .*generated' "$WORK/cpack.log" | head -1)"
DEB=$(ls -t "$BUILD"/*.deb 2>/dev/null | head -1)
[ -n "$DEB" ] && [ -f "$DEB" ]
check $? "and it is where it says it is" "${DEB:-nothing found}"
[ -n "$DEB" ] || exit 1

echo
echo "What it declares"
CONTROL=$(dpkg-deb -I "$DEB")
VERSION=$(sed -n 's/^ Version: //p' <<<"$CONTROL")
CMAKE_VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
[ "$VERSION" = "$CMAKE_VERSION" ]
check $? "the package version is the project version" "$VERSION against $CMAKE_VERSION in CMakeLists.txt"

# The run-time plugin sets. `shlibdeps` cannot find these and a package without
# them is the one that launches and plays nothing.
for set in plugins-base plugins-good plugins-bad plugins-ugly; do
    grep -q "gstreamer1.0-$set" <<<"$CONTROL"
    check $? "depends on gstreamer1.0-$set"
done
grep -q "libqt6quick6" <<<"$CONTROL"
check $? "and on Qt Quick, which dpkg-shlibdeps found by reading the binary"

echo
echo "What it contains"
CONTENTS=$(dpkg-deb -c "$DEB")
for path in usr/bin/ferrolux \
            usr/share/applications/ferrolux.desktop \
            usr/share/icons/hicolor/scalable/apps/ferrolux.svg; do
    grep -q "$path" <<<"$CONTENTS"
    check $? "ships $path"
done

echo
echo "Whether it runs anywhere but the build tree (BUG-023)"
ROOT="$WORK/root"
mkdir -p "$ROOT"
dpkg-deb -x "$DEB" "$ROOT"
check $? "the package extracts"

export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"
mkdir -p "$XDG_CONFIG_HOME/ferrolux"
printf '[playback]\nvolume=0\n' > "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini"

TONE="$WORK/tone.flac"
gst-launch-1.0 -q audiotestsrc num-buffers=1200 wave=0 freq=440 volume=0.3 \
    ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
    ! flacenc ! filesink location="$TONE" >/dev/null 2>&1

"$ROOT/usr/bin/ferrolux" --play "$TONE" >"$WORK/player.log" 2>&1 &
D="--session --dest org.mpris.MediaPlayer2.ferrolux --object-path /org/mpris/MediaPlayer2"
prop() { gdbus call $D --method org.freedesktop.DBus.Properties.Get \
             org.mpris.MediaPlayer2.Player "$1" 2>/dev/null | sed 's/^(<//; s/>,)$//'; }

for _ in $(seq 1 40); do [ -n "$(prop PlaybackStatus)" ] && break; sleep 0.5; done
[ -n "$(prop PlaybackStatus)" ]
check $? "the installed binary starts and answers on the bus" \
      "this is the check BUG-023 did not have"

[ "$(prop PlaybackStatus)" = "'Playing'" ]
check $? "and plays the file it was given" "$(prop PlaybackStatus)"

first=$(prop Position | sed 's/^int64 //')
sleep 2
second=$(prop Position | sed 's/^int64 //')
[ -n "$first" ] && [ -n "$second" ] && [ "$second" -gt "$first" ]
check $? "with the position advancing, so it is decoding and not merely open" \
      "$((first / 1000)) ms to $((second / 1000)) ms"

gdbus call $D --method org.mpris.MediaPlayer2.Quit >/dev/null 2>&1
sleep 2
! pgrep -x ferrolux >/dev/null
check $? "and quits when asked"

! grep -qiE "cannot|could not|no such|failed" "$WORK/player.log"
check $? "with nothing complaining in its output" \
      "$(grep -iE 'cannot|could not|no such|failed' "$WORK/player.log" | head -1)"

printf '\n'
if [ "$failures" -eq 0 ]; then
    echo "PASS — the package builds, declares what it needs, and plays music from outside its build tree"
else
    echo "FAILED ($failures)"
fi
printf 'note: nothing was installed. The dependency list is read rather than\n'
printf '      resolved, so a missing plugin set would not be caught here — only\n'
printf '      a real install on a machine without it would do that.\n'
exit $((failures > 0))
