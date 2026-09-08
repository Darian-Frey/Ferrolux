#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# AV-007 detection: is the meter texture uploaded from the render thread?
#
# Scene graph resources may only be touched during synchronisation and
# rendering. `MeterTexture` is built so that they are — the band values are
# staged into a plain image on the GUI thread, and the texture is created inside
# a slot direct-connected to `beforeSynchronizing`, which the scene graph emits
# on the render thread with the GUI thread blocked.
#
# **The reason this needs a tool is that the failure is invisible where it is
# written.** Under the basic render loop the render thread *is* the GUI thread,
# so uploading from a bus handler is not merely undetected, it is correct; the
# same code then corrupts or crashes under the threaded loop, on a driver the
# author does not have. Qt issues no warning either way. So this forces
# `QSG_RENDER_LOOP=threaded`, and refuses to report a pass unless the run
# actually saw two distinct threads — a green result obtained under the basic
# loop would be the exact false negative the vector describes.
#
# It runs under both RHI backends for the same reason. AV-007 calls the failure
# a latent portability one: what a backend does with a texture handed to it from
# the wrong thread is the backend's business, and OpenGL is the forgiving one.
# Vulkan is where it shows, and the validation layer is asked for by name.
#
#   usage: tools/verify-render-thread.sh [seconds] [path-to-ferrolux]
#          default: 12 seconds per backend, build-release/ferrolux

set -u

RUN_SECONDS="${1:-12}"
PLAYER="${2:-build-release/ferrolux}"
[ -x "$PLAYER" ] || { echo "no player at $PLAYER" >&2; exit 2; }
PLAYER=$(readlink -f "$PLAYER")

[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || {
    echo "needs a display; the threaded render loop is the whole point" >&2; exit 2; }

pgrep -x ferrolux >/dev/null && {
    echo "a ferrolux is already running; this needs the instance it starts itself" >&2
    exit 2
}

WORK=$(mktemp -d)
export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"
mkdir -p "$XDG_CONFIG_HOME/ferrolux" "$XDG_DATA_HOME"
cat > "$XDG_CONFIG_HOME/ferrolux/ferrolux.ini" <<CONF
[playback]
volume=0
CONF

# The logs are the evidence, and a failure is the one case where somebody wants
# to read them — so a failing run keeps them and says where.
failures=0

cleanup() {
    pkill -x ferrolux 2>/dev/null
    sleep 1
    if [ "${failures:-0}" -eq 0 ]; then
        rm -rf "$WORK"
    else
        printf 'logs kept in %s\n' "$WORK"
    fi
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

# Something for the meters to be fed by. A texture is only uploaded when there
# are band values to upload, so a silent run would report zero uploads and no
# violations — which is a pass by vacancy, and the check below for a non-zero
# upload count exists to refuse it.
TONE="$WORK/tone.flac"
gst-launch-1.0 -q audiotestsrc num-buffers=2600 wave=sine freq=440 volume=0.4 \
    ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
    ! flacenc ! filesink location="$TONE" >/dev/null 2>&1

run_backend() {  # run_backend <rhi backend>
    local backend="$1"
    LOG="$WORK/$backend.log"

    # `QSG_RHI_DEBUG_LAYER` asks Vulkan for its validation layer, which is what
    # turns a wrong-thread resource touch from undefined behaviour into a
    # message. It is ignored by the OpenGL backend.
    QSG_RENDER_LOOP=threaded \
    QSG_RHI_BACKEND="$backend" \
    QSG_RHI_DEBUG_LAYER=1 \
    QSG_INFO=1 \
    FERROLUX_RENDER_CHECK="$RUN_SECONDS" \
        "$PLAYER" --play "$TONE" >"$LOG" 2>&1
    STATUS=$?

    RENDER_LINE=$(grep -m1 '^RENDER ' "$LOG")
    UPLOADS=$(sed -n 's/.*[^_]uploads=\([0-9]*\).*/\1/p' <<<"$RENDER_LINE")
    ON_GUI=$(sed -n 's/.*uploads_on_gui=\([0-9]*\).*/\1/p' <<<"$RENDER_LINE")
    STAGES=$(sed -n 's/.*stages=\([0-9]*\).*/\1/p' <<<"$RENDER_LINE")
    OFF_GUI=$(sed -n 's/.*stages_off_gui=\([0-9]*\).*/\1/p' <<<"$RENDER_LINE")
    DISTINCT=$(sed -n 's/.*threads_distinct=\([a-z]*\).*/\1/p' <<<"$RENDER_LINE")
    # Qt says which loop and which backend it actually got, and neither is what
    # the environment asked for if the driver declined.
    LOOP=$(grep -m1 -o "threaded render loop\|basic render loop" "$LOG")
    # Count what the layer *said*, not that it exists. Qt announces
    # "Graphics API debug/validation layers: 1" under QSG_INFO whenever the
    # layer is enabled, and the first version of this counted that line — so
    # asking for validation was itself the finding, on both backends. A real
    # complaint from Vulkan carries a VUID identifier or the words "Validation
    # Error"; Qt's own scene graph errors say so in as many words.
    VALIDATION=$(grep -Evi "debug/validation layers" "$LOG" \
                 | grep -Eci "VUID-|validation error|scene graph.*error" || true)
}

assess() {  # assess <rhi backend>
    local backend="$1"
    # A crash is a finding, not an inconvenience, and it must not be reported as
    # a missing backend. Introducing the violation this tool looks for — making
    # the synchronising connection queued, so the slot runs on the GUI thread —
    # segfaults the process within a second under the threaded loop, before any
    # counter can be printed. That is AV-007 happening rather than AV-007 going
    # unmeasured, and the two need different words.
    if [ "$STATUS" -ge 128 ]; then
        check 1 "$backend: the player crashed" \
              "signal $((STATUS - 128)) after ${UPLOADS:-0} uploads — a wrong-thread \
resource touch is one cause; see $LOG"
        return
    fi
    if [ -z "${RENDER_LINE:-}" ]; then
        check 1 "$backend: the run reported nothing" \
              "exit $STATUS, no RENDER line — the backend is probably unavailable here"
        return
    fi
    printf '  %s\n  loop: %s\n' "$RENDER_LINE" "${LOOP:-not stated}"

    # The precondition. Everything below is meaningless without it, so it is
    # checked first and named for what it protects against rather than for what
    # it observes.
    check "$([ "${DISTINCT:-no}" = "yes" ] && echo 0 || echo 1)" \
          "$backend: the render thread is a different thread from the GUI thread" \
          "otherwise this backend's result is a false negative, not a pass"

    check "$([ "${UPLOADS:-0}" -gt 0 ] && echo 0 || echo 1)" \
          "$backend: textures were actually uploaded" \
          "${UPLOADS:-0} uploads over ${RUN_SECONDS}s"

    check "$([ "${ON_GUI:-1}" -eq 0 ] && echo 0 || echo 1)" \
          "$backend: no upload happened on the GUI thread" \
          "${ON_GUI:-?} of ${UPLOADS:-?}"

    check "$([ "${OFF_GUI:-1}" -eq 0 ] && echo 0 || echo 1)" \
          "$backend: and no staging happened off it" \
          "${OFF_GUI:-?} of ${STAGES:-?}"

    check "$([ "${VALIDATION:-0}" -eq 0 ] && echo 0 || echo 1)" \
          "$backend: the graphics layer reported nothing" \
          "${VALIDATION:-0} validation lines"
}

for backend in opengl vulkan; do
    printf '\n== %s, threaded render loop ==\n' "$backend"
    run_backend "$backend"
    assess "$backend"
done

printf '\n'
if [ "$failures" -eq 0 ]; then
    echo "PASS — the meter texture is uploaded only from the render thread, on both backends"
else
    echo "FAILED ($failures)"
fi
exit $((failures > 0))
