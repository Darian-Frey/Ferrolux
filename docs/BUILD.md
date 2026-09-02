# Build

Written from the first successful build on 2026-09-02, per ROADMAP.md Phase 1.
Everything below has been run; where a value differs from what was originally
specified, the specified value was wrong and this document is authoritative.

---

## Supported platforms

Linux only for RS-1, per D-009. Both X11 and Wayland sessions are supported.

**Verified configuration.** Phase 1 builds and its acceptance harness passes on:

| Component | Specified | Actually used |
|-----------|-----------|---------------|
| Distribution | — | Ubuntu 24.04 LTS (X11 session) |
| C++ compiler | GCC 12 / Clang 15 | GCC 13.3.0 |
| CMake | 3.21 | 3.28.3 |
| Ninja | 1.10 | 1.11.1 |
| Qt | ~~6.5~~ **6.4** | 6.4.2 |
| GStreamer | 1.20 | 1.24.2 |
| TagLib | 1.12 | not yet required (Phase 2) |

**The Qt minimum is 6.4, not 6.5.** Ubuntu 24.04 and the Linux Mint 22 series
derived from it ship 6.4.2 and offer nothing later, so the previously stated 6.5
minimum could not be met on this project's own reference platform. Phase 1 uses
no Qt feature newer than 6.2. This is tracked as BUG-001, which also records a
second and more serious version constraint: SPEC.md's Handjet axis values need
`QFont::setVariableAxis`, introduced in Qt 6.7, and that question comes due in
Phase 5.

**Reference hardware.** Performance targets in FEATURES.md and ROADMAP.md are
measured on a ThinkPad P15 Gen 2i: Intel i7-11850H, NVIDIA T1200 with 4 GB VRAM,
Linux Mint. Deliberately a mid-range mobile workstation rather than a fast
desktop, so that the 60 fps meter target means something.

## Dependencies

### Debian, Ubuntu, Linux Mint

Phase 1 needs only the following. This is the exact set that produced the
verified build:

```bash
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-libav \
  gstreamer1.0-pipewire
```

Later phases add `libtag1-dev` (Phase 2, metadata) and `qt6-shadertools-dev`
(Phase 4, meter shaders). Neither is needed to build Phase 1, and adding them
early only lengthens the first build.

`gstreamer1.0-plugins-bad` is required rather than optional: it carries several
of the codecs in F-001's acceptance list, and it provides `audiomixmatrix`,
which implements the balance control. `gstreamer1.0-libav` covers the remainder.

### Fedora

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtdeclarative-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good gstreamer1-plugins-bad-free
```

Untested. Codec availability on Fedora is more restricted than on Debian
derivatives; some formats in F-001 will require third-party repositories. This
belongs in the packaging notes rather than being worked around in code.

### Arch

```bash
sudo pacman -S base-devel cmake ninja \
  qt6-base qt6-declarative \
  gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav
```

Untested.

## Build commands

```bash
# Debug build
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run — an optional file argument is loaded but not started
./build-debug/ferrolux ~/Music/some-album/track.flac
```

## Tests

The Phase 1 acceptance harness drives `core/Engine` headlessly against the
criteria in ROADMAP.md Phase 1. It needs two real audio files and so is
registered with CTest only when both are named at configure time:

```bash
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFERROLUX_TEST_FLAC=/path/to/test.flac \
  -DFERROLUX_TEST_MP3=/path/to/test-vbr.mp3
ctest --test-dir build-debug --output-on-failure
```

It can also be run directly, which is more useful while developing because the
per-check output is not swallowed:

```bash
./build-debug/acceptance_transport /path/to/test.flac /path/to/test-vbr.mp3
```

Suitable test files can be generated without hunting for material. Thirty-two
seconds of stereo at 44.1 kHz, one FLAC and one genuinely variable-bitrate MP3:

```bash
gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! flacenc ! filesink location=test.flac

gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! lamemp3enc target=quality quality=4 ! filesink location=test-vbr.mp3
```

`lamemp3enc` comes from `gstreamer1.0-plugins-ugly`, which is a test-only
dependency and is not required to build or run Ferrolux.

### Development environment settings

Force the threaded render loop during development. The basic loop hides
render-thread violations that will crash on other machines — see AV-007.

```bash
export QSG_RENDER_LOOP=threaded
```

For pipeline debugging:

```bash
export GST_DEBUG=3                    # warnings and errors
export GST_DEBUG=ferrolux:5,level:4   # per-element detail
export GST_DEBUG_DUMP_DOT_DIR=/tmp    # writes pipeline graphs
```

Qt categorised logging follows the module names in ARCHITECTURE.md. Only
`ferrolux.core` exists so far:

```bash
export QT_LOGGING_RULES="ferrolux.*=true"
```

Meter and position logging will be off by default when they exist, because both
fire at frame rate.

## Cross-compilation

Not supported and not planned for RS-1, per D-009. Platform-specific code is
confined to `platform/` so that a future port is bounded work, but no
cross-compilation toolchain is maintained.

## Troubleshooting

**`could not link the audio filter chain`, and balance does nothing.**
`audiomixmatrix` refuses to link while its `matrix` property is empty, and it
declares `channels: [1, MAX]` on its pads, so nothing else in the chain pins the
channel count. Both must be handled: set the matrix before linking, and put a
capsfilter forcing `audio/x-raw,channels=2` ahead of it. Without the capsfilter
a mono source negotiates one channel against an element configured for two, and
the failure surfaces at `set_caps` as `Erroneous matrix detected` rather than at
link time. `core/Engine.cpp` does both; this note exists because the symptom
points at the wrong place.

**`No such file or directory` loading `Main.qml` from a `qrc:` path.**
Qt 6.4 places QML module resources under `/<URI>/` while Qt 6.5 and later
default to `/qt/qml/<URI>/`. `CMakeLists.txt` pins `RESOURCE_PREFIX /qt/qml` so
the load path does not depend on the Qt version. Confirm what was actually
built with `strings build-debug/ferrolux | grep qt/qml`.

**A VBR MP3 reports no duration immediately after preroll, then reports one
slightly too long.** Both are expected. An MP3 without a Xing or VBRI header
cannot answer a duration query until enough of the stream has been seen, and the
answer is then extrapolated from the observed bitrate. The measured overshoot on
the generated test file is about 2.4 seconds on 32.5. Anything depending on
duration must tolerate `-1` and must re-read after `DURATION_CHANGED`.

**No audio output, pipeline reaches PLAYING.** Check which sink
`autoaudiosink` selected with `GST_DEBUG=3`. On systems with both PipeWire and a
stale PulseAudio configuration the selection can be wrong; specifying the sink
explicitly during development isolates this.

**Format plays in another player but not here.** A missing plugin set rather
than a code problem. `gst-inspect-1.0 | grep <codec>` establishes whether the
decoder is present at all.

**Crash only under the threaded render loop.** This is AV-007 and is a real
defect, not an environment problem. Do not work around it by switching render
loops.
