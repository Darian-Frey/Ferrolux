# Build

**This document is provisional.** It describes the intended toolchain and dependency set. Per ROADMAP.md Phase 1, it is to be rewritten from the actual build the moment the first build succeeds, while the details are still recoverable. Treat anything below as a starting point to be corrected, not as a verified recipe.

---

## Supported platforms

Linux only for RS-1, per D-009. Both X11 and Wayland sessions are supported and tested.

**Reference hardware.** Performance targets in FEATURES.md and ROADMAP.md are measured on a ThinkPad P15 Gen 2i: Intel i7-11850H, NVIDIA T1200 with 4 GB VRAM, Linux Mint. This is deliberately a mid-range mobile workstation rather than a fast desktop, so that the 60 fps meter target means something.

**Toolchain versions.**

| Component | Minimum | Notes |
|-----------|---------|-------|
| C++ compiler | GCC 12 or Clang 15 | C++20 required |
| CMake | 3.21 | For Qt 6 integration and presets |
| Ninja | 1.10 | Generator of choice |
| Qt | 6.5 | Base, Declarative, ShaderTools |
| GStreamer | 1.20 | Core plus plugin sets below |
| TagLib | 1.12 | Metadata reading |

## Dependencies

### Debian, Ubuntu, Linux Mint

```bash
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-shadertools-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtqml-workerscript \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-libav \
  gstreamer1.0-pipewire \
  libtag1-dev
```

The `bad` plugin set is required rather than optional — it carries several of the codecs in F-001's acceptance list. `gstreamer1.0-libav` covers the remainder.

### Fedora

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtshadertools-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good gstreamer1-plugins-bad-free \
  taglib-devel
```

Codec availability on Fedora is more restricted than on Debian derivatives; some formats in F-001 will require third-party repositories. This should be documented in the packaging notes rather than worked around in code.

### Arch

```bash
sudo pacman -S base-devel cmake ninja \
  qt6-base qt6-declarative qt6-shadertools \
  gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav \
  taglib
```

## Build commands

```bash
# Debug build
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Tests
ctest --test-dir build-debug --output-on-failure

# Run
./build/ferrolux [files or directories]
```

### Development environment settings

Force the threaded render loop during development. The basic loop hides render-thread violations that will crash on other machines — see AV-007.

```bash
export QSG_RENDER_LOOP=threaded
```

For pipeline debugging:

```bash
export GST_DEBUG=3                    # warnings and errors
export GST_DEBUG=ferrolux:5,level:4   # per-element detail
export GST_DEBUG_DUMP_DOT_DIR=/tmp    # writes pipeline graphs
```

Qt categorised logging follows the module names in ARCHITECTURE.md:

```bash
export QT_LOGGING_RULES="ferrolux.*=true;ferrolux.meters=false"
```

Meter and position logging are off by default because both fire at frame rate.

## Cross-compilation

Not supported and not planned for RS-1, per D-009. Platform-specific code is confined to `platform/` so that a future port is bounded work, but no cross-compilation toolchain is maintained.

## Troubleshooting

**No audio output, pipeline reaches PLAYING.** Check which sink `autoaudiosink` selected with `GST_DEBUG=3`. On systems with both PipeWire and a stale PulseAudio configuration the selection can be wrong; specifying the sink explicitly during development isolates this.

**Shaders fail to compile at build time.** `qsb` runs as part of the build and reports GLSL errors with a line offset from the preprocessed source, not the original file. Compile the shader manually with `qsb --glsl 100es,120,150 --hlsl 50 --msl 12 -o out.qsb in.frag` to get readable diagnostics.

**Meters render but never update.** Almost always a render-thread issue rather than a data issue — check whether the texture update is reaching the scene graph, before checking whether the bus messages are arriving. Verify with `GST_DEBUG=level:5` that messages are being posted at all.

**Format plays in another player but not here.** A missing plugin set rather than a code problem. `gst-inspect-1.0 | grep <codec>` establishes whether the decoder is present at all.

**Crash only under the threaded render loop.** This is AV-007 and is a real defect, not an environment problem. Do not work around it by switching render loops.
