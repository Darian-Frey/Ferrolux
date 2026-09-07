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
  qml6-module-qtquick-dialogs \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-libav \
  gstreamer1.0-pipewire
```

Later phases add `libtag1-dev` (Phase 2, metadata) and `qt6-shadertools-dev`
(Phase 4, meter shaders). Neither is needed to build Phase 1, and adding them
early only lengthens the first build.

**Everything through Phase 5 needs this much**, which is the list above plus:

```bash
sudo apt install libtag1-dev qt6-shadertools-dev
```

Qt 6's OpenGL and D-Bus modules are wanted too. Both come with `qt6-base-dev`
on these distributions, so there is nothing extra to install, but each is a
named component in `CMakeLists.txt`. `tests/frame_bench` renders through
`QQuickRenderControl` on the OpenGL RHI and links OpenGL directly; D-Bus
carries the MPRIS2 service of F-050, and only the application target links it —
no test suite does, which is the point of keeping it inside `platform/`.

A machine with no session bus still builds and still plays. The service says so
and carries on: losing the desktop's media controls is not a reason to refuse
to play music.

**X11 is optional and is found rather than required.** Where it is present the
media keys gain a last-resort global grab for sessions with no settings daemon —
a bare window manager, typically (IMP-008) — and `libx11-dev` supplies it, which
`qt6-base-dev` already pulls in on these distributions. Where it is absent, on a
Wayland-only or headless machine, the build says so and the class compiles to a
stub; nothing else changes.

**The two measurement tools need more, and none of it is needed to build or
run the player.** `tools/measure-frames.sh` and `tools/verify-scaling.sh` drive
a real window and read back what was drawn, so they want a display, a window
manager and:

```bash
sudo apt install wmctrl x11-utils imagemagick python3-pil
```

`tools/make-test-fixtures.sh` generates one file per format in F-001's
acceptance list. Two of them need encoders GStreamer does not have — `wavpack`
and `mpcenc`, from the `wavpack` and `musepack-tools` packages. Neither is a
dependency of Ferrolux; the script skips those two fixtures with a note when
they are absent.

`tools/make-fonts.sh` additionally needs `curl`, `unzip` and `fonttools`. It
builds a virtualenv for the last of these if it is not importable, so there is
usually nothing to install — and it only needs running when the bundled faces
are regenerated, which is rarely.

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

## Installing

```bash
cmake --install build --prefix ~/.local
```

That puts the binary in `bin/`, the desktop entry in
`share/applications/ferrolux.desktop` and a scalable icon in
`share/icons/hicolor/scalable/apps/`. A launcher picks it up from there, and so
does a file manager's "Open with" — the entry declares the same media types the
MPRIS service advertises, and `tests/spec_test` holds the two lists to each
other.

Opening a file from a file manager goes through the same path as a second
launch: the entry's `Exec` runs `ferrolux` with the file, which hands it to the
player already running rather than starting a second one (F-052).

**Run the installed binary at least once before believing a packaging change.**
BUG-023 was an untested dependency on the build directory — the application had
never worked when installed, and nothing noticed for five phases because every
test ran it where it was built.

## Build commands

```bash
# Debug build
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run — paths fill the playlist and select the first track, without starting it
./build-debug/ferrolux ~/Music/some-album/track.flac
```

## Tests

Eight suites, 379 checks. Six are self-contained and need nothing but the build;
`tokens_test` and `spec_test` take the source directory, because they read the
tree rather than the build: `spec_test` holds SPEC.md §Settings and the code to
each other in both directions, so an undocumented key and a documented one that
nothing implements are each a failing check rather than something to be noticed
by grep. `tokens_test` reads the token sets and the faces from the tree:

```bash
./build-debug/playlist_model_test
./build-debug/equaliser_test
./build-debug/meters_test
./build-debug/tokens_test .
```

The other two need real audio files, and so are registered with CTest **only
when both are named at configure time**. Without them `ctest` reports four
suites passing rather than six, which reads as a pass and is not one — check the
count, not the colour.

The Phase 1 acceptance harness drives `core/Engine` headlessly against the
criteria in ROADMAP.md Phase 1:

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

# The same again, but with a Xing header, which is what real encoders emit
gst-launch-1.0 -q audiotestsrc num-buffers=1400 wave=ticks samplesperbuffer=1024 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! lamemp3enc target=quality quality=4 ! xingmux ! filesink location=test-vbr-xing.mp3
```

Both MP3s are worth keeping. `lamemp3enc` alone produces a headerless VBR file
whose tag-declared duration is 20% too long, which is the awkward real-world case
SPEC.md §Duration describes; adding `xingmux` produces the ordinary case where the
tag is exact. `tests/metadata_reader_test` takes the FLAC, the headerless MP3 and
optionally the Xing one, and asserts the right behaviour for each.

A third fixture is useful once the meters exist — a steady sine whose peak sits
at −18 dBFS, so its RMS is 3 dB below the VU reference and the needle settles at
a known 0.707:

```bash
gst-launch-1.0 -q audiotestsrc num-buffers=400 wave=sine freq=1000 volume=0.12589 \
  ! audioconvert ! audio/x-raw,rate=44100,channels=2 \
  ! flacenc ! filesink location=tone-ref.flac
```

`acceptance_transport` takes it as an optional third argument and falls back to
the first file without it.

`lamemp3enc` comes from `gstreamer1.0-plugins-ugly`, which is a test-only
dependency and is not required to build or run Ferrolux.

### Measurements

Two tools rather than tests, because each needs a display, a window manager and
a GPU, and a machine with none of those would report a failure that says nothing
about the code. Both are AV detections and both are run by hand.

```bash
./tools/measure-frames.sh 4      # AV-002: 60 fps, in a window and offscreen at 4K
./tools/verify-scaling.sh        # AV-005: the panel at 1x, 1.5x, 2x and 3x
./tools/verify-desktop.sh        # IMP-010: platform/ against a real session
```

`verify-desktop.sh` is a tool for the same reason as the other two: an MPRIS
service needs a session bus and a single-instance hand-off needs two processes,
so a build machine with neither would report a failure that says nothing about
the code. It needs `gdbus` and a session bus, and no display — the player is
asked to quit over MPRIS rather than having its window closed, which is also
what keeps it honest, since SIGTERM never reaches `aboutToQuit` and a tool that
kills the process is checking that nothing was saved.

**It is hermetic.** `XDG_CONFIG_HOME` and `XDG_DATA_HOME` point into a
temporary directory, so the settings it writes, the session it saves and the
playlist it leaves behind are its own, and running it cannot disturb yours.

`measure-frames.sh` runs two passes. The window pass measures the whole
application at the sizes the display can render; the offscreen pass renders the
meter display alone through `QQuickRenderControl`, with a fence after each
frame, which is the only way to reach 3840×2160 on a smaller screen and the only
way to get a headroom figure that is not a lower bound.

`verify-scaling.sh` captures the panel at each device pixel ratio and measures
the 10–90% rise across a chassis-to-well edge in device pixels — the
resampling-artefact check as a number — then reduces each capture back to 1× to
confirm it is the same panel rather than a different layout that happens to fit.
Set `FERROLUX_SCALING_SHOTS` to a directory to keep the captures; the numbers
can tell crisp from soft and cannot tell a substituted face from a correct one.

Both tools have reported defects that turned out to be their own: a window
manager clamping a window to one monitor, a maximised window silently ignoring a
resize, a GPU heated by the previous mode in the same sweep. Each is documented
in the tool and in the attack vector it serves. A measurement that cannot say
what it measured is worse than none, because it will be believed.

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
