# Ferrolux

> **Status:** Active
> **Provenance:** Shane Hartley (author, primary developer); Claude (documentation scaffolding, design review)
> **Last reviewed:** 2026-09-02
> **Why this status:** Project initialised 2026-09-02. Documentation scaffold complete; Phase 1 implementation not yet started.

Ferrolux RS-1 is a full-featured audio player for Linux with a cassette futurism interface — the visual language of late-1970s and 1980s high-end tape decks, rendered as resolution-independent vector chrome rather than bitmap skins. It covers the same ground as Winamp did: transport, playlist management, a ten-band equaliser, and switchable VU and spectrum displays. It is aimed at people who want a local-file player with physical-instrument character on a modern high-DPI desktop, and its distinguishing choice is that the entire panel is drawn rather than blitted, so it is correct at any scale.

The model code **RS-1** is the badge on the panel and the window title. Repository tags follow semantic versioning; see [DECISIONS.md](DECISIONS.md) D-008.

## Quick start

Not yet available — Phase 1 (transport core) has not been implemented. Once it exists:

```bash
git clone https://github.com/Darian-Frey/ferrolux.git
cd ferrolux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ferrolux ~/Music/some-album/
```

## Build requirements

- Linux (X11 or Wayland). No Windows or macOS target in RS-1 — see D-009.
- Qt 6.5 or later: Base, Declarative, ShaderTools, Multimedia not required.
- GStreamer 1.20 or later, with `base`, `good`, `bad` and `libav` plugin sets.
- TagLib 1.12 or later.
- CMake 3.21+, Ninja, a C++20 compiler (GCC 12+ or Clang 15+).

Full setup instructions, per-distribution package lists and troubleshooting are in [BUILD.md](BUILD.md).

## Project structure

```
ferrolux/
├── src/
│   ├── core/          # Playback engine, GStreamer pipeline, EQ control
│   ├── meters/        # Level and spectrum acquisition, ballistics, GPU upload
│   ├── library/       # Playlist model, metadata, playlist file I/O
│   ├── platform/      # Settings, MPRIS, media keys, single-instance
│   └── main.cpp
├── qml/
│   ├── panel/         # Panel chrome, transport, playlist, equaliser
│   ├── meters/        # Display modes (spectrum, VU, ladder, oscilloscope)
│   └── shaders/       # .frag / .vert sources, compiled by qsb
├── resources/         # Fonts, panel art, presets
├── tests/
├── tools/
└── docs/              # This documentation set
```

## Documentation map

| Document | Covers |
|----------|--------|
| [FEATURES.md](FEATURES.md) | Capabilities, priorities, acceptance criteria, out of scope |
| [ROADMAP.md](ROADMAP.md) | Phased plan from transport core to RS-1 release |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Module boundaries, data flow, invariants |
| [DECISIONS.md](DECISIONS.md) | Design decisions with rationale and reversal conditions |
| [SPEC.md](SPEC.md) | Equaliser constants, meter data contract, pipeline definition, file formats |
| [ATTACK_VECTORS.md](ATTACK_VECTORS.md) | Failure modes and detection methods |
| [BUGS.md](BUGS.md) | Realised defects |
| [IMPROVEMENTS.md](IMPROVEMENTS.md) | Candidate refactors and code-quality work |
| [BUILD.md](BUILD.md) | Toolchain, dependencies, build and test commands |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [CLAUDE.md](CLAUDE.md) | Handoff document for AI-assisted development sessions |

## Licence

Not yet chosen. See [DECISIONS.md](DECISIONS.md) D-010, which is **Proposed** rather than Accepted and must be settled before the first public commit of source code. Qt's open-source licensing and the GStreamer plugin sets in use both constrain the choice.
