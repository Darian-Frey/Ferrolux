# Ferrolux

> **Status:** Active
> **Provenance:** Shane Hartley (author, primary developer); Claude (documentation scaffolding, design review)
> **Last reviewed:** 2026-09-02
> **Why this status:** Project initialised 2026-09-02. Documentation set complete; Phase 1 (transport core) complete and verified on the same date. Phase 2 (playlist) feature-complete: model, play order, metadata, file I/O, filtering and gapless all implemented and tested. Two acceptance clauses — scroll frame time and an audible gapless join — remain unverified; see ROADMAP.md Phase 2. Phase 3 (equaliser) feature-complete: bands, preamp, headroom, bypass, gain ramping, presets and `.eqf` import all implemented and tested. Its two audible acceptance clauses — no denormal stalls, no zipper noise — remain unverified for the same reason Phase 2's do.

Ferrolux RS-1 is a full-featured audio player for Linux with a cassette futurism interface — the visual language of late-1970s and 1980s high-end tape decks, rendered as resolution-independent vector chrome rather than bitmap skins. It covers the same ground as Winamp did: transport, playlist management, a ten-band equaliser, and switchable VU and spectrum displays. It is aimed at people who want a local-file player with physical-instrument character on a modern high-DPI desktop, and its distinguishing choice is that the entire panel is drawn rather than blitted, so it is correct at any scale.

The model code **RS-1** is the badge on the panel and the window title. Repository tags follow semantic versioning; see [DECISIONS.md](DECISIONS.md) D-008.

## Quick start

Ferrolux plays audio and manages a playlist. There is no panel yet — the
interface is a plain harness until Phase 5.

```bash
git clone https://github.com/Darian-Frey/Ferrolux.git
cd Ferrolux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ferrolux ~/Music/some-album/track.flac
```

## Build requirements

- Linux (X11 or Wayland). No Windows or macOS target in RS-1 — see D-009.
- Qt 6.4 or later: Base and Declarative. Shader Tools from Phase 4. Multimedia not required.
- GStreamer 1.20 or later, with `base`, `good`, `bad` and `libav` plugin sets.
- TagLib 1.12 or later, from Phase 2.
- CMake 3.21+, Ninja, a C++20 compiler (GCC 12+ or Clang 15+).

The Qt figure is 6.4 rather than the 6.5 originally specified: Ubuntu 24.04 and
Linux Mint 22 ship 6.4.2 and offer nothing later, so the reference platform could
not meet the higher figure. See BUG-001.

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

GPL-3.0-or-later, settled on 2026-09-02. See [DECISIONS.md](DECISIONS.md) D-010 for the reasoning and for the dependency audit that established nothing in the stack forced the choice. Bundled fonts are separately under the SIL Open Font License 1.1 per D-012.
