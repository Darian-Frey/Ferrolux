# CLAUDE.md

Handoff document for AI-assisted development sessions. This is current state, not history — rewrite it when state changes rather than appending.

## Project summary

Ferrolux RS-1 is a Winamp-scope audio player for Linux with a cassette futurism interface, built on Qt 6 with QML and a GStreamer audio pipeline. It provides transport, playlist management, a ten-band equaliser and switchable shader-rendered VU and spectrum displays. Its defining constraint is that all chrome is drawn as vectors and shaders rather than bitmap skins, so the interface is correct at any display scale.

## Current state

Phase 1 complete as of 2026-09-02. The application builds, plays audio and
passes its acceptance harness.

- `CMakeLists.txt` — Qt 6.4 and GStreamer discovery, one executable plus one test
- `src/core/Engine.{h,cpp}` — `playbin3` wrapper, state machine, taper and pan
- `src/main.cpp` — entry point, settings persistence, single file argument
- `qml/Main.qml` — throwaway Phase 1 harness, plain Qt Quick Controls
- `tests/acceptance_transport.cpp` — headless Phase 1 acceptance harness
- `docs/` — this document set
- `LICENSE` — GPL-3.0-or-later, settled 2026-09-02 (D-010). Source files carry SPDX headers
- Not yet present: `meters/`, `library/`, `platform/`, `resources/`, `tools/`

Two open defects, both found during Phase 1 and neither blocking it. **BUG-001**:
the documented Qt minimum was unattainable on the reference platform, and
SPEC.md's Handjet axis values need Qt 6.7 — due at Phase 5. **BUG-002**: SPEC.md
gives balance a law but no pipeline element, filled provisionally with
`audiomixmatrix` and awaiting a ruling.

## Active task

Phase 2, playlist. Deliver F-010 through F-014 and F-005: `library/PlaylistModel`
as a `QAbstractListModel` with asynchronous metadata population, TagLib on a
worker thread, multi-select and drag reorder with single-level undo, shuffle as a
permutation, M3U/M3U8/PLS load and save, sort and live filter, and gapless
advance via `about-to-finish`.

Acceptance: a 20,000-entry playlist loads, scrolls at 60 fps, sorts in under a
second, and survives a full shuffle pass with no repeats before exhaustion. A
known-gapless album plays through with no audible join. See ROADMAP.md Phase 2.

Phase 2 needs `libtag1-dev`, which is not yet installed.

Two loose ends inherited from Phase 1 land here. `Engine::previousTrackRequested()`
is emitted and nothing consumes it; the playlist model is its consumer. The Next
button in the QML harness is wired to stop and needs real behaviour once play
order exists.

## Architectural invariants

These are the rules that must not be violated. The full descriptions are in ARCHITECTURE.md §Key invariants.

1. No application work on GStreamer streaming threads — no allocation, locking against UI state, blocking I/O or synchronous cross-thread signalling.
2. GStreamer object ownership is confined to `core/`. No `GstElement*` outside that directory.
3. Texture uploads happen only on the render thread, inside scene graph synchronisation and render callbacks.
4. Playback position is polled once per rendered frame at most, and all consumers read the same cached value.
5. The playlist model owns play order. Shuffle is a permutation held by the model, not a per-advance random choice.
6. No fixed-size bitmap assets in the control surface.
7. Equaliser gain changes never restart the pipeline.
8. Every gain path has stated headroom and cannot hard clip.

## Build and test

Verified working. Full dependency lists and troubleshooting are in BUILD.md.

```bash
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
./build-debug/ferrolux path/to/track.flac
```

The acceptance harness needs two real audio files, so it takes them as
arguments rather than being self-contained:

```bash
./build-debug/acceptance_transport path/to/test.flac path/to/test-vbr.mp3
```

BUILD.md §Tests gives `gst-launch-1.0` recipes for generating both. Registering
it with CTest requires `-DFERROLUX_TEST_FLAC=` and `-DFERROLUX_TEST_MP3=` at
configure time.

Use `QSG_RENDER_LOOP=threaded` during development — the basic loop hides the
render-thread violations described in AV-007.

## Conventions

- **British English** in all documentation, comments and user-facing strings.
- **ISO 8601 dates** everywhere.
- **Stable IDs** are append-only: `F-` features, `D-` decisions, `AV-` attack vectors, `BUG-`, `IMP-`. Withdrawn entries get a status flag; they are never deleted or renumbered, because other documents reference them.
- **Commit messages** are multi-paragraph: a summary line, then the reasoning, then the affected IDs. Reference `F-`, `D-`, `AV-`, `BUG-` and `IMP-` IDs so the history is traceable from the documents and vice versa.
- **Documents are part of the commit.** A code change that invalidates a document without updating it is an incomplete commit.
- **Constants live in SPEC.md**, not in comments and not duplicated in the README. Code should reference the spec section; the spec should not be re-derived from the code.
- **Design tokens, never literal colours** in QML. See SPEC.md §Design tokens.
- C++20. Qt naming conventions in C++, QML conventions in QML; do not mix the two styles within a file.

## Known pitfalls

See ATTACK_VECTORS.md for the canonical list. The ones most likely to bite during Phase 1 specifically:

- **AV-001** is easiest to violate while the codebase is small and the threading model feels theoretical. Bus handlers run on the main loop, but probe callbacks do not — check which context any new callback runs in before writing anything into it.
- **AV-007** hides itself. Texture upload from the wrong thread often works under the basic render loop and fails under the threaded one. Force `QSG_RENDER_LOOP=threaded` during development so the bug surfaces immediately rather than on someone else's machine.
- **AV-012** is a coupling trap: the VU and spectrum paths share `MeterSource`, so a smoothing change made for one silently changes the other. The VU ballistics filter has a specified response and should be tested independently of anything the spectrum display needs.
- Position polling is the most common accidental performance defect in players of this shape. One poller, cached, per frame.

## Out of scope

Do not change these without asking.

- **Winamp classic skin support.** Permanently out of scope per D-003 and FEATURES.md §Out of scope. This is the decision the project is built on; it is not open for re-litigation.
- **Adding a library database, tag editing, video, or streaming service integration.** All explicitly excluded. Candidate features in FEATURES.md are ideas, not permission.
- **Changing the equaliser band layout or range.** Fixed by D-007 for preset compatibility.
- **Replacing the GStreamer backend or the Qt/QML choice.** D-001 and D-002 are Accepted; the reversal conditions in those entries are the only route to reopening them.
- **Relicensing.** D-010 is Accepted as GPL-3.0-or-later. Do not change it, and do not add differently-licensed code to the tree. New source files get an `SPDX-License-Identifier: GPL-3.0-or-later` header and a copyright line.
- **Accepting outside contributions.** Merging third-party code permanently removes the author's ability to relicense, which D-010 flags as the point of no return for the hardware companion candidate. A CLA or DCO policy is the author's decision and is not yet settled.
- **Promoting candidate features into the roadmap.** Requires a recorded decision first.

Per Maintenance Rule 8: when you discover a bug or notice an improvement candidate while working on something else, log it in BUGS.md or IMPROVEMENTS.md rather than fixing or applying it inline. The author decides whether to act immediately, defer or decline. This rule exists specifically because AI partners default to acting on discoveries, and the value of those catalogues depends on their completeness.
