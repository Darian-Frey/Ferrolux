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
