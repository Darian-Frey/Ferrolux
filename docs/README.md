# Ferrolux

> **Status:** Active
> **Provenance:** Shane Hartley (author, primary developer); Claude (documentation scaffolding, design review)
> **Last reviewed:** 2026-09-07
> **Why this status:** Phases 1 to 6 are built and Phase 7 is open.
> Transport, a 20,000-entry
> playlist, a ten-band equaliser, five shader-rendered displays and the
> cassette futurism panel itself all work, in four finishes with the display
> invertible. 425 checks across eight suites pass in Debug and Release.
>
> Four criteria are measured rather than asserted, and they are the reasons
> the project exists. AV-002: every display holds 60 fps at 3840×2160 with at
> least 40% of the frame budget spare, by `tools/measure-frames.sh`. AV-005: the
> panel is correct at 1×, 1.5×, 2× and 3× device pixel ratio, by
> `tools/verify-scaling.sh`. AV-001: with every core spinning and the disc
> busy, the application's own work on the audio thread peaks at 4.6 µs against
> a 1 ms budget and the sink never runs dry, by `tools/stress-audio.sh`.
> AV-007: with the threaded render loop forced, no meter texture upload happens
> on the GUI thread under either RHI backend, by
> `tools/verify-render-thread.sh`.
>
> Every one of those tools first reported a defect that turned out to be its
> own — a clamped window, a maximised one, an overheated GPU, a benchmark
> drawing with undefined parameters, a timer that blamed the application for
> GStreamer's milliseconds. Each is recorded, because a measurement that cannot
> say what it measured is worse than none.
>
> Five clauses remain unverified. Four are audible or need a harness that does
> not exist: playlist scroll frame time, denormal stalls, the gapless join and
> the absence of zipper noise. The fifth is Phase 5's second acceptance clause,
> which asks whether a viewer reads the panel as photographed hardware, and is
> a judgement for the author rather than a measurement.
>
> One open bug and two suggested improvements. Thirty-two found, thirty
> fixed and one won't-fix upstream. BUG-027 remains at low severity, narrowed to
> a next playlist entry whose type is recognised and whose contents will not
> decode, which costs the track before it 1.46 s of its tail. Phase 6 is complete; Phase 7 is under way, and its first work is
> the Must-priority acceptance clauses earlier phases left Partial — F-001,
> **Every Must-priority feature is Complete**, F-032 having closed on
> 2026-09-08. One acceptance clause in the project is still open and it belongs
> to a phase rather than a feature: ROADMAP.md Phase 4 asks whether the VU
> needle is visually indistinguishable from a reference deck. That is a
> judgement for the author, and the measurable obstacle to it — a face that did
> not crowd — is gone. **All four Critical attack vectors
> now have implemented detection**, AV-007 having closed on 2026-09-08.

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
│   ├── meters/        # Acquisition, ballistics, GPU upload, frame timing
│   ├── library/       # Playlist model, metadata, playlist file I/O
│   ├── ui/            # Token sets, and the display proportions of F-036
│   ├── app/           # Player — the wiring, and the only module joining peers
│   ├── platform/      # Settings, session, MPRIS2, keys, single instance, CLI
│   └── main.cpp       # Process startup, QML context, fonts, command line
├── qml/               # Panel components, flat — Main, PanelSection, Slot, …
│   └── shaders/       # .frag sources, compiled by qsb at build time
├── resources/
│   ├── fonts/         # The four OFL faces of D-012, with their licences
│   └── themes/        # Token sets: ferric, anodised, glacier, ember
├── tests/             # Eight suites, plus frame_bench which is a tool
├── tools/             # Fixtures, fonts, and the AV-002 and AV-005 measurements
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
| [BENCHMARKS.md](BENCHMARKS.md) | Every measured claim, from one tree in one session |
| [BUGS.md](BUGS.md) | Realised defects |
| [IMPROVEMENTS.md](IMPROVEMENTS.md) | Candidate refactors and code-quality work |
| [BUILD.md](BUILD.md) | Toolchain, dependencies, build and test commands |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [CLAUDE.md](CLAUDE.md) | Handoff document for AI-assisted development sessions |

## Licence

GPL-3.0-or-later, settled on 2026-09-02. See [DECISIONS.md](DECISIONS.md) D-010 for the reasoning and for the dependency audit that established nothing in the stack forced the choice. Bundled fonts are separately under the SIL Open Font License 1.1 per D-012.
