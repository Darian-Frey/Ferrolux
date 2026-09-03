# CLAUDE.md

Handoff document for AI-assisted development sessions. This is current state, not history — rewrite it when state changes rather than appending.

## Project summary

Ferrolux RS-1 is a Winamp-scope audio player for Linux with a cassette futurism interface, built on Qt 6 with QML and a GStreamer audio pipeline. It provides transport, playlist management, a ten-band equaliser and switchable shader-rendered VU and spectrum displays. Its defining constraint is that all chrome is drawn as vectors and shaders rather than bitmap skins, so the interface is correct at any display scale.

## Current state

Phases 1 to 3 feature-complete as of 2026-09-02. Phase 4 in progress. The
application builds, plays audio, manages a 20,000-entry playlist, equalises it
and meters it, behind a deliberately plain harness. 222 checks across five test
suites pass in both Debug and Release.

- `CMakeLists.txt` — Qt 6.4, GStreamer, GStreamer-app and TagLib discovery
- `src/core/Engine.{h,cpp}` — `playbin3`, state machine, taper and pan, gapless
  handover, and parsing of the `level` and `spectrum` element messages
- `src/core/Equaliser.{h,cpp}` — ten bands behind a decibel-only abstraction,
  preamp, bypass, presets, `.eqf` import, 30 ms gain ramp
- `src/library/` — `PlaylistModel` (contents *and* play order), `MetadataReader`
  (TagLib on a private pool), `PlaylistIO` (M3U/M3U8/PLS), `PlaylistFilter`
- `src/meters/MeterSource.{h,cpp}` — bucketing, smoothing, peak-hold, VU
  ballistics, and the scheduling queue that corrects the message lead
- `src/main.cpp` — entry point, all inter-module wiring, settings persistence
- `qml/Main.qml` — throwaway harness; Phase 5 deletes it
- `tests/` — `acceptance_transport`, `playlist_model_test`,
  `metadata_reader_test`, `equaliser_test`, `meters_test`
- `LICENSE` — GPL-3.0-or-later (D-010). Source files carry SPDX headers
- Not yet present: `platform/`, `resources/`, `tools/`, and any shader

**No open bugs.** Thirteen found so far, twelve fixed and one won't-fix
(BUG-006, an upstream GStreamer defect worked around). **No suggested
improvements**: two applied, one declined, two deferred against recorded
triggers. Read BUGS.md before changing the equaliser or the meters — several
entries record specification faults that looked entirely reasonable until
measured.

Four acceptance clauses across Phases 2 and 3 remain unverified, and they do
**not** all need the same instrument, which is easy to assume and wrong.

- *Playlist scroll frame time* is the one AV-002's frame timing can answer, and
  the timing now exists. What is still missing is a way to drive a 20,000-entry
  scroll under it — a test harness, not another instrument.
- *Denormal stalls* are an audio-thread cost, not a rendering one. A stall
  shows as a dropout, not a dropped frame, and needs the processing time of the
  pipeline measured against its buffer deadline.
- *The audible gapless join* and *the audible absence of zipper noise* both need
  a capture of the sink and an analysis of the samples, which AV-006 describes
  and nothing has yet built.

## Active task

Phase 5, the panel. Phase 4 closed on 2026-09-03 with every display measured at
3840x2160 rather than assumed: five modes, 60 fps, between 46% and 64% of the
frame budget spare against a requirement of 30%. `tools/measure-frames.sh` is
the instrument — a window pass over the whole application and an offscreen
`QQuickRenderControl` pass over the meter display alone — and running it after a
shader change is cheap enough that there is no excuse for not doing it.

Two things from Phase 4 that will bite in Phase 5 if forgotten:

- **The meter texture's alpha channel is not spare.** The scene graph
  premultiplies on upload, so any value but 255 scales R, G and B — and R and G
  are the magnitude. Data a shader needs per frame goes in a uniform. BUG-016.
- **For a layered shader, quiet is the expensive case, not loud.** Coverage lets
  a pixel stop early; empty space does not. A benchmark fed a loud signal will
  report a pass that means nothing, which is how BUG-016 nearly survived its own
  detection. The synthetic material in `tests/frame_bench` sweeps the level for
  this reason, and anything measuring a new panel element should too.

`qml/Main.qml` remains the throwaway harness (D-003, F-040). `qml/MeterDisplay.qml`
does not: it is the first piece of the real panel, and it is what the benchmark
measures.

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
./build-debug/ferrolux ~/Music          # a file, a folder, or nothing
```

Three suites are self-contained. The other two need real audio files and take
them as arguments:

```bash
./build-debug/meters_test
./build-debug/playlist_model_test
./build-debug/equaliser_test
./build-debug/metadata_reader_test <flac> <vbr-mp3> [vbr-mp3-with-xing]
./build-debug/acceptance_transport <flac> <vbr-mp3> [reference-tone]
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
