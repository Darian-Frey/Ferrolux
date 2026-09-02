# CLAUDE.md

Handoff document for AI-assisted development sessions. This is current state, not history — rewrite it when state changes rather than appending.

## Project summary

Ferrolux RS-1 is a Winamp-scope audio player for Linux with a cassette futurism interface, built on Qt 6 with QML and a GStreamer audio pipeline. It provides transport, playlist management, a ten-band equaliser and switchable shader-rendered VU and spectrum displays. Its defining constraint is that all chrome is drawn as vectors and shaders rather than bitmap skins, so the interface is correct at any display scale.

## Current state

Documentation only. There is no source code, no build system and no executable.

- `docs/` — this document set, complete as of 2026-09-02
- `LICENSE` — **absent**, blocking. See DECISIONS.md D-010
- Everything under `src/`, `qml/`, `tests/`, `tools/` and `resources/` described in README.md §Project structure is planned, not present

## Active task

Phase 1, transport core. Deliver F-001 through F-004: a CMake skeleton with Qt 6 and GStreamer discovery, a `core/Engine` wrapping `playbin3` with an explicit state machine, single-poll position reporting, cubic volume taper and constant-power balance, and a throwaway QML harness with five buttons and a position bar.

Acceptance: a FLAC and a VBR MP3 both play end to end, seek accurately, pause and resume cleanly, and survive twenty stop-start cycles without leaking or hanging the pipeline. See ROADMAP.md Phase 1.

Write BUILD.md the moment the first build succeeds, while the toolchain details are still fresh.

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

Not yet available. Phase 1 establishes these; update this section as soon as they exist.

```bash
# Intended shape
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

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
- **Choosing a licence.** D-010 is Proposed and is the author's decision, not an implementation detail. Do not add a `LICENSE` file on your own initiative.
- **Promoting candidate features into the roadmap.** Requires a recorded decision first.

Per Maintenance Rule 8: when you discover a bug or notice an improvement candidate while working on something else, log it in BUGS.md or IMPROVEMENTS.md rather than fixing or applying it inline. The author decides whether to act immediately, defer or decline. This rule exists specifically because AI partners default to acting on discoveries, and the value of those catalogues depends on their completeness.
