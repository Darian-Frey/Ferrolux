#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# F-041 and AV-005: is the panel correct at every device pixel ratio, or only at
# the one it was drawn at.
#
# This is the acceptance criterion the project exists for. Ferrolux began as an
# intention to fix classic-skin scaling and became a bespoke panel because that
# defect cannot be fixed in an engine: a 275x116 bitmap resampled to a modern
# display either blurs or goes blocky, and no amount of work on the resampler
# changes what the source material can support. Drawing the chrome instead is
# only worth anything if the result actually holds up, so this measures whether
# it does rather than asserting it.
#
# Two questions, because "looks fine" conflates them.
#
#   crispness  does an edge stay one pixel wide as the ratio rises? Resampled
#              chrome spreads its edges in proportion to the scale; vector
#              geometry and a fragment shader antialiased from the screen-space
#              derivative do not. Measured as the 10-90% rise distance across
#              the boundary between the chassis and a lit well, in *device*
#              pixels, which should stay near 1 at every ratio.
#   fidelity   is it the same panel, or a different layout that happens to fit?
#              Each capture is reduced to the 1x size and compared with the 1x
#              capture. A panel that scales continuously reduces back to itself;
#              one that snaps to whole multiples, reflows, or rounds its metrics
#              to a pixel grid does not, and the difference says how far off.
#
# The window is sized in logical units so that every ratio draws the same panel.
# 3x of the default 720x780 would be 2340 pixels tall and taller than this
# screen, and the window manager would clamp it — the run would then compare a
# 3x panel against a 1x one and call the difference a defect. See AV-002 for
# the same trap in the frame measurement.
#
# The default logical size is derived from the display rather than chosen,
# because the constraint is the display's and not the panel's. A window manager
# clamps a window to the *monitor* it is on, not to the whole desktop: on this
# machine the desktop is 1920x2120 across two stacked screens and a window is
# still confined to 1048 rows of one of them. Ask for a size the largest ratio
# cannot fit and the run silently compares a 3x panel against a 1x one and
# reports the difference as a defect, which is how the first run of this script
# read. Same trap as AV-002, one directory along.
#
#   usage: tools/verify-scaling.sh [logical-width] [logical-height]

set -e
cd "$(dirname "$0")/.."

MAX_RATIO=3

# The smallest connected monitor is what bounds the run, with an allowance for
# the frame and whatever panels the desktop keeps at its edges.
FIT=$(xrandr 2>/dev/null | awk '
    /\bconnected/ && match($0, /[0-9]+x[0-9]+\+[0-9]+\+[0-9]+/) {
        split(substr($0, RSTART, RLENGTH), a, /[x+]/)
        if (w == 0 || a[1] < w) w = a[1]
        if (h == 0 || a[2] < h) h = a[2]
    }
    END { printf "%d %d", w, h }')
FIT_W=${FIT% *}
FIT_H=${FIT#* }
[ "${FIT_W:-0}" -gt 0 ] || { FIT_W=1920; FIT_H=1080; }

DEFAULT_W=$(( (FIT_W - 40) / MAX_RATIO ))
DEFAULT_H=$(( (FIT_H - 90) / MAX_RATIO ))

WIDE="${1:-$DEFAULT_W}"
HIGH="${2:-$DEFAULT_H}"

echo "monitor ${FIT_W}x${FIT_H}; testing a ${WIDE}x${HIGH} logical panel up to ${MAX_RATIO}x"
BINARY=./build-release/ferrolux
TRACK=fixtures/tone-ref.flac

[ -x "$BINARY" ] || { echo "build the release target first" >&2; exit 2; }
[ -n "$DISPLAY" ] || { echo "needs a display; this measures real rendering" >&2; exit 2; }
command -v wmctrl >/dev/null || { echo "needs wmctrl" >&2; exit 2; }
command -v import >/dev/null || { echo "needs ImageMagick" >&2; exit 2; }

# A number is not a look. Set FERROLUX_SCALING_SHOTS to a directory to keep the
# captures: the measurements below can tell crisp from soft and one layout from
# another, and cannot tell whether a face has been substituted or a colour has
# come out wrong, which are the failures a person spots at a glance.
if [ -n "$FERROLUX_SCALING_SHOTS" ]; then
    SHOTS="$FERROLUX_SCALING_SHOTS"
    mkdir -p "$SHOTS"
    trap 'pkill -x ferrolux 2>/dev/null || true' EXIT
else
    SHOTS=$(mktemp -d)
    trap 'rm -rf "$SHOTS"; pkill -x ferrolux 2>/dev/null || true' EXIT
fi

for FACTOR in 1 1.5 2 3; do
    QT_SCALE_FACTOR="$FACTOR" "$BINARY" "$TRACK" >/dev/null 2>&1 &
    sleep 3

    WID=$(wmctrl -lx | awk '$3 ~ /^ferrolux\./ {print $1; exit}')
    if [ -z "$WID" ]; then
        echo "no window at ${FACTOR}x" >&2
        pkill -x ferrolux || true
        continue
    fi

    # wmctrl works in device pixels; the window is being given the same logical
    # size at every ratio, which is the whole point of the comparison.
    DW=$(awk "BEGIN { printf \"%d\", $WIDE * $FACTOR }")
    DH=$(awk "BEGIN { printf \"%d\", $HIGH * $FACTOR }")

    # Unmaximise first. The window opens at its default logical size, which at
    # 3x is 2160x2340 device pixels and larger than the screen, so the manager
    # maximises it on the way up — and a maximised window ignores a resize
    # request entirely. Without this the 3x run came back at the full work area
    # and was reported as clamped, which blamed the display for something the
    # script had done to itself.
    wmctrl -i -r "$WID" -b remove,maximized_vert,maximized_horz
    sleep 1
    wmctrl -i -r "$WID" -e "0,40,40,$DW,$DH"
    sleep 2

    import -window "$WID" "$SHOTS/scale-$FACTOR.png"
    pkill -x ferrolux || true
    sleep 1
done

python3 - "$SHOTS" "$WIDE" "$HIGH" <<'PY'
import sys, glob, os

try:
    from PIL import Image
except ImportError:
    print("needs python3-pil for the analysis", file=sys.stderr)
    sys.exit(2)

shots, wide, high = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])

SHELL = (0xB4, 0xB2, 0xA9)
WELL = (0x2C, 0x2C, 0x2A)


def luma(p):
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]


def rise(image):
    """10-90% rise distance across the first chassis-to-well edge, in device
    pixels, averaged over the scanlines that have one. A hard edge rendered
    correctly transitions within about a pixel however large the pixel is."""
    px = image.convert("RGB").load()
    w, h = image.size
    hi, lo = luma(SHELL), luma(WELL)
    top, bottom = hi - 0.1 * (hi - lo), lo + 0.1 * (hi - lo)

    widths = []
    for y in range(h // 4, 3 * h // 4, 3):
        started = None
        for x in range(w - 1):
            v = luma(px[x, y])
            if started is None and v >= top:
                started = x
            elif started is not None:
                if v <= bottom:
                    widths.append(x - started)
                    break
                if v >= top:
                    started = x
    if not widths:
        return None
    widths.sort()
    return widths[len(widths) // 2]


ordered = sorted(glob.glob(os.path.join(shots, "scale-*.png")),
                 key=lambda p: float(os.path.basename(p)[6:-4]))
if not ordered:
    print("no captures")
    sys.exit(1)

base = Image.open(ordered[0]).convert("RGB")

print()
print(f"{'ratio':>6} {'captured':>12} {'expected':>12} {'edge rise':>10} "
      f"{'vs 1x':>8}  verdict")

failed = 0
for path in ordered:
    factor = float(os.path.basename(path)[6:-4])
    image = Image.open(path).convert("RGB")
    expected = (int(wide * factor), int(high * factor))

    edge = rise(image)

    # Reduced to the 1x size and compared. A panel that scales continuously
    # comes back to itself; one that reflows or snaps does not.
    reduced = image.resize(base.size, Image.LANCZOS)
    diff = sum(abs(a - b)
               for pa, pb in zip(reduced.getdata(), base.getdata())
               for a, b in zip(pa, pb)) / (base.size[0] * base.size[1] * 3)

    sized = abs(image.size[0] - expected[0]) <= 2 and abs(image.size[1] - expected[1]) <= 2
    crisp = edge is not None and edge <= 2
    faithful = diff <= 12.0

    verdict = ("holds" if (sized and crisp and faithful)
               else "CLAMPED" if not sized
               else "SOFT" if not crisp
               else "REFLOWED")
    if verdict != "holds":
        failed = 1

    print(f"{factor:>5}x {str(image.size):>12} {str(expected):>12} "
          f"{('%d px' % edge) if edge is not None else '  n/a':>10} "
          f"{diff:>8.2f}  {verdict}")

print()
if failed:
    print("FAIL — the panel is not correct at every ratio tested")
else:
    print("PASS — one panel, drawn correctly at every ratio tested")
    print("note: edge rise is the 10-90% transition across a chassis-to-well")
    print("      boundary in device pixels. It stays near 1 because the chrome")
    print("      is geometry rather than a resampled image; a bitmap skin would")
    print("      show it growing with the ratio, which is AV-005.")
    print("      'vs 1x' is the mean per-channel difference after reducing the")
    print("      capture back to 1x. It is not zero and should not be: more")
    print("      pixels means genuinely more detail, and reducing that back")
    print("      loses some of it.")
sys.exit(failed)
PY
