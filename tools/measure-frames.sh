#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# AV-002 detection: does each display mode hold 60 fps, at three resolutions.
#
# What this measures, and what it cannot.
#
# The interval between frames answers the acceptance criterion directly — 60 fps
# held, or not — together with the count of frames that arrived late enough to
# have missed a refresh. Both are honest.
#
# Headroom is harder. Disabling the swap interval does not turn the interval
# into the true cost of a frame: the compositor paces frames regardless, and the
# result is not a faster run but an erratic one, with the worst interval jumping
# from 34 ms to nearly 400. So headroom is reported from the CPU render pass
# only, which is a *lower bound* — GPU execution is submitted asynchronously and
# is not in it. Read a high figure as "the CPU is not the problem", never as
# "there is room to spare".
#
# Sizes are limited by the display. The window manager clamps a window to the
# screen, an off-screen window receives no frame callbacks and renders nothing,
# and the offscreen platform plugin loads the software backend, which does not
# execute the shaders at all. The `size` column is therefore what was actually
# rendered, not what was asked for.
#
# So this runs two passes, which answer different halves of the question.
#
#   window     the real application in a real window, at the sizes this display
#              can render. The whole scene — meters, playlist, chrome, bindings
#              — and therefore the number that describes what a user sees.
#   offscreen  tests/frame_bench, rendering the meter display alone through
#              QQuickRenderControl at any size, with a fence after each frame.
#              No window manager to clamp the size and no compositor to pace the
#              loop, so this one reaches 3840x2160 and its headroom is real.
#
# Neither is sufficient alone. The window pass measures everything but cannot
# reach 4K or see past vsync; the offscreen pass reaches 4K with true headroom
# but measures only the part whose cost scales with pixels. Together they cover
# the Phase 4 acceptance criterion.
#
#   usage: tools/measure-frames.sh [seconds-per-run] [audio-file]

set -e
SECONDS_PER_RUN="${1:-6}"
TRACK="${2:-fixtures/tone-ref.flac}"
BUDGET_MS=16.667
BINARY=./build-release/ferrolux
BENCH=./build-release/frame_bench

[ -x "$BINARY" ] || { echo "build the release target first: cmake --build build-release" >&2; exit 2; }
[ -x "$BENCH" ] || { echo "build the release target first: cmake --build build-release" >&2; exit 2; }
[ -f "$TRACK" ] || { echo "no audio at $TRACK — run tools/make-test-fixtures.sh" >&2; exit 2; }
[ -n "$DISPLAY" ] || { echo "needs a display; this measures real rendering" >&2; exit 2; }

CONFIG="$HOME/.config/ferrolux/ferrolux.ini"
SAVED=$(mktemp); [ -f "$CONFIG" ] && cp "$CONFIG" "$SAVED"

echo '== window pass: the whole application, at the sizes this display can render =='
printf '%-16s %-11s %11s %10s %10s %9s %5s %s\n' \
       mode size interval_ms render_ms worst_ms cpu_head late verdict

FAILED=0
for MODE in spectrum spectrum-mirror flame vu ladder; do
  for SIZE in 1280x720 1600x900 1920x1080; do
    python3 - "$CONFIG" "$MODE" <<'PY'
import configparser, pathlib, sys
p = pathlib.Path(sys.argv[1]); p.parent.mkdir(parents=True, exist_ok=True)
c = configparser.ConfigParser(); c.optionxform = str
if p.exists(): c.read(p)
if 'meters' not in c: c['meters'] = {}
c['meters']['mode'] = sys.argv[2]
with p.open('w') as f: c.write(f)
PY

    # The run ends itself after the measurement window and writes its report;
    # SIGTERM would not reach aboutToQuit, so killing it would produce nothing.
    LOG=$(mktemp)
    FERROLUX_FRAME_MEASURE="$SECONDS_PER_RUN" FERROLUX_GEOMETRY="$SIZE" \
      QSG_RENDER_LOOP=threaded "$BINARY" "$TRACK" >"$LOG" 2>&1 || true

    LINE=$(grep '^FRAMES ' "$LOG" | tail -1)
    if [ -z "$LINE" ]; then
      printf '%-16s %-11s %s\n' "$MODE" "$SIZE" "no report — the run produced none"
      FAILED=1
    else
      eval "$(echo "$LINE" | sed 's/^FRAMES //; s/ms\b//g')"
      # `size` comes from the report, not from the request: the window manager
      # clamps to the display, so the two differ whenever the screen is smaller.
      SIZE_SHOWN="$size"

      # Holding 60 fps means frames arriving at about the budget, and almost
      # none of them late. A little slack on the interval: the compositor's
      # pacing is not exact, and a frame at 17.5 ms has dropped nothing.
      VERDICT=$(awk -v i="$interval_mean" -v l="$late" -v f="$frames" -v b="$BUDGET_MS" \
        'BEGIN { ok = (i+0 <= b*1.10) && (f+0 > 0) && (l+0 <= f*0.02); print ok ? "holds" : "MISSED" }')
      printf '%-16s %-11s %11s %10s %10s %9s %5s %s\n' \
             "$MODE" "$SIZE_SHOWN" "$interval_mean" "$render_mean" "$render_worst" \
             "$cpu_headroom" "$late" "$VERDICT"
      [ "$VERDICT" = "holds" ] || FAILED=1
    fi
    rm -f "$LOG"
  done
done

[ -f "$SAVED" ] && cp "$SAVED" "$CONFIG"; rm -f "$SAVED"

# The offscreen pass. Same instrument, no window: this is where 3840x2160 and
# the 30% headroom clause are actually decided. frame_bench prints its own
# table and exits non-zero if any mode fails either clause.
echo
echo '== offscreen pass: the meter display alone, unpaced, at full resolution =='
"$BENCH" 1920 1080 600 || FAILED=1
"$BENCH" 3840 2160 600 || FAILED=1

echo
if [ "$FAILED" -eq 0 ]; then
  echo "PASS — every mode holds 60 fps in both passes, including 3840x2160"
  echo "note: in the window pass, cpu_head is the CPU render pass only and"
  echo "      excludes GPU execution, so it is a lower bound. The offscreen"
  echo "      pass fences each frame, so its headroom figure is the real one."
  echo "      A 'holds*' there means the mean and the late count both pass but"
  echo "      one frame ran over budget — this loop shares the GPU with the"
  echo "      desktop, so a lone spike is not attributable to the shader."
else
  echo "FAIL — a mode did not hold 60 fps"
fi
exit "$FAILED"
