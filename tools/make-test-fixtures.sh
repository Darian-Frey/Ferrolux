#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# Generates the audio fixtures the test suites take as arguments. They are
# synthetic and deterministic, so two runs are comparable, and none of them is
# committed — see BUILD.md §Tests for what each one is for.
#
#   usage: tools/make-test-fixtures.sh [directory]     (default: ./fixtures)

set -e
DIR="${1:-fixtures}"
mkdir -p "$DIR"


# ---- F-001's format list -------------------------------------------------
# One short file per format the feature claims, so "plays FLAC, MP3, Ogg
# Vorbis, Opus, AAC/M4A, WAV, AIFF, WavPack, Musepack and ALAC" can be checked
# rather than assumed. Two of them need encoders GStreamer does not have, and
# are skipped with a note rather than silently missing:
#
#   wavpack        `wavpack`        (apt: wavpack)
#   musepack       `mpcenc`         (apt: musepack-tools)
#
# Neither is a dependency of Ferrolux. They are here for the same reason
# `make-fonts.sh` wants fonttools: to regenerate an input, not to build.
formats() {
    # The sample format is left to `audioconvert` to negotiate, because the
    # encoders disagree about it — `vorbisenc` takes float and refuses S16LE,
    # and forcing one format on all of them silently produces no file. Two
    # exceptions are pinned deliberately:
    #
    #   Opus refuses 44.1 kHz outright and is resampled to 48.
    #   WAV is pinned to 16-bit PCM, because `wavenc` writes IEEE float by
    #   default — valid, but an unusual thing to hold a decoder to, and the
    #   reference WavPack encoder refuses it outright.
    SRC="audiotestsrc num-buffers=300 wave=ticks samplesperbuffer=1024 ! audioconvert"

    gst-launch-1.0 -q $SRC ! vorbisenc ! oggmux ! filesink location="$DIR/format.ogg"
    gst-launch-1.0 -q $SRC ! audioresample ! audio/x-raw,rate=48000 \
        ! opusenc ! oggmux ! filesink location="$DIR/format.opus"
    gst-launch-1.0 -q $SRC ! avenc_aac ! mp4mux ! filesink location="$DIR/format-aac.m4a"
    gst-launch-1.0 -q $SRC ! avenc_alac ! mp4mux ! filesink location="$DIR/format-alac.m4a"
    gst-launch-1.0 -q $SRC ! avmux_aiff ! filesink location="$DIR/format.aiff"
    gst-launch-1.0 -q $SRC ! audio/x-raw,format=S16LE,rate=44100,channels=2 \
        ! wavenc ! filesink location="$DIR/format.wav"
    echo "  format.{ogg,opus,m4a,wav,aiff}  Vorbis, Opus, AAC, ALAC, AIFF, WAV"

    if command -v wavpack >/dev/null; then
        wavpack -q -y "$DIR/format.wav" -o "$DIR/format.wv" 2>/dev/null
        echo "  format.wv            WavPack, from the reference encoder"
    else
        echo "  format.wv            SKIPPED — install wavpack"
    fi

    if command -v mpcenc >/dev/null; then
        mpcenc --quiet "$DIR/format.wav" "$DIR/format.mpc" 2>/dev/null
        echo "  format.mpc           Musepack SV8, from mpcenc"
    else
        echo "  format.mpc           SKIPPED — install musepack-tools"
    fi

    # The other half of F-001: a file that cannot be decoded must produce a
    # visible error and advance the playlist rather than stalling. Three ways of
    # being unplayable, because they fail at three different points — the
    # typefinder, the decoder, and the filesystem.
    head -c 200000 /dev/urandom > "$DIR/broken-random.flac"
    head -c 60000 "$DIR/test.flac" > "$DIR/broken-truncated.flac"
    echo "  broken-*.flac        random bytes, and a FLAC cut short"
}

echo "writing fixtures to $DIR"

# 32.5 s of ticks: broadband transients, deterministic, and awkward in the ways
# real material is not — nearly silent between ticks.
gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! flacenc ! filesink location="$DIR/test.flac"
echo "  test.flac            FLAC, 32.5 s"

# The same, as VBR MP3 with no Xing header. Its tag-declared duration is about
# 20% too long, which is the awkward real-world case SPEC.md §Duration covers.
gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! lamemp3enc target=quality quality=4 ! filesink location="$DIR/test-vbr.mp3"
echo "  test-vbr.mp3         VBR MP3, no Xing header — duration reads long"

# With a Xing header, which is what real encoders emit and where the tag is exact.
gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! lamemp3enc target=quality quality=4 ! xingmux ! filesink location="$DIR/test-vbr-xing.mp3"
echo "  test-vbr-xing.mp3    VBR MP3 with Xing — duration exact"

# A steady sine whose peak sits at -18 dBFS, so its RMS is 3 dB below the VU
# reference and the needle settles at a known 0.707.
gst-launch-1.0 -q audiotestsrc num-buffers=400 wave=sine freq=1000 volume=0.12589 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! flacenc ! filesink location="$DIR/tone-ref.flac"
echo "  tone-ref.flac        1 kHz at -18 dBFS peak — VU settles at 0.707"

cat <<USAGE

run the suites with:
  ./build-debug/metadata_reader_test $DIR/test.flac $DIR/test-vbr.mp3 $DIR/test-vbr-xing.mp3
  ./build-debug/acceptance_transport $DIR/test.flac $DIR/test-vbr-xing.mp3 $DIR/tone-ref.flac
USAGE

formats
