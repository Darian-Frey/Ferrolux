# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com).
Versioning is semantic. The RS-1 model code is a panel badge and does
not track the version number — see DECISIONS.md D-008.

Entries reference F-, D-, AV-, BUG- and IMP- IDs for traceability.

## [Unreleased]

### Added
- Initial documentation scaffold: README, FEATURES, ROADMAP, ARCHITECTURE,
  DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, BUILD, CLAUDE.
- D-001 through D-009 and D-011 recorded as Accepted; D-010 (licence)
  recorded as Proposed and outstanding.
- F-001 through F-052 defined with priorities and acceptance criteria.
- AV-001 through AV-013 identified ahead of implementation; detection
  is `not implemented` for all but AV-013.
- D-012 recorded as Accepted: four SIL Open Font License 1.1 faces, one
  per type role, chosen against a drawn-geometry alternative that
  remains the recorded reversal path.
- SPEC.md gains a Typography subsection under §Design tokens, defining
  the `type-readout-numeric`, `type-readout-text`,
  `type-readout-segment` and `type-legend` tokens, Handjet's
  variable-axis values, the unlit-segment ghost-layer rendering
  requirement, and the exclusion of pixel-grid faces from the control
  surface per AV-005.

- Phase 1 (transport core) complete: a CMake project building a Qt 6 / QML
  application over a GStreamer `playbin3` pipeline, delivering F-003 in full
  and F-001, F-002 and F-004 in part.
- `core/Engine` with a five-state machine documented in its header, owning
  every GStreamer object per ARCHITECTURE.md invariant 2, with position and
  duration polled once per rendered frame per invariant 4.
- Cubic volume taper and constant-power balance per SPEC.md §Volume taper;
  balance is an `audiomixmatrix` diagonal, see BUG-002.
- `tests/acceptance_transport`, a headless harness covering every clause of
  the ROADMAP.md Phase 1 acceptance criterion.
- BUG-001 and BUG-002 logged, both found during Phase 1.

- D-010 accepted: the project is licensed **GPL-3.0-or-later**. `LICENSE`
  added with the unmodified GPLv3 text; SPDX headers and copyright lines added
  to every source file. A dependency audit recorded in D-010 established that
  Qt, GStreamer core and every plugin in the pipeline are LGPL, so no upstream
  term forced the outcome.

- Phase 2 feature-complete: playlist file I/O for M3U, M3U8 and PLS; a live
  filter proxy that provably leaves play order alone; gapless advance through
  `about-to-finish`; and a harness with a virtualised playlist, multi-select,
  drag reorder, sort menu, and open/save.
- `library/PlaylistIO`, `library/PlaylistFilter`, and `PlaylistModel::loadFrom`
  / `saveTo`. 60 checks in `playlist_model_test`, 10 in `metadata_reader_test`,
  9 more for the gapless handover in `acceptance_transport`.
- `playback/shuffle` and `playback/repeat` now persist, per SPEC.md §Settings.

- `PlaylistModel::moveSelection` completes F-011: a non-contiguous selection
  moves as one block, preserving relative order, the current entry, and any
  shuffle permutation. Ten checks cover the permutation independently of the
  drag interaction that drives it.

- `core/Equaliser`: ten bands at Winamp's centres behind a decibel-only
  abstraction per D-006, ±12 dB preamp with headroom management, bypass, a
  nine-curve preset bank, and a Winamp `.eqf` decoder. 30 checks in
  `tests/equaliser_test`.
- AV-003 detection implemented — the first Critical attack vector to move from
  `not implemented` to a real check, and it found BUG-005 on its first run.

- IMP-003 applied: `PlaylistModel::moveSelection` tested selection membership
  against a sorted list once per row, making a large multi-row move quadratic.
  A `QBitArray` mask makes both passes linear — 790 ms to 8 ms in Debug, 66 ms
  to 1 ms in Release, for a 10,000-row selection on 20,000 entries.
- IMP-002 declined, IMP-004 and IMP-005 deferred with recorded triggers: a
  fifth test suite for IMP-004, and Phase 6's four consumers for IMP-005.
- IMP-001 applied: `MetadataReader` now reports completion. `idle()` fires when
  every enqueued batch has delivered, rather than only when work is cancelled,
  and `progressChanged(completed, total)` is added. Previously the outstanding
  count was incremented and never decremented, so the state read as if
  completion were tracked when it was not.

- Phase 3 feature-complete: a 30 ms gain ramp, user preset storage, `.eqf`
  import and a full equaliser panel in the harness. Equaliser state persists
  under the SPEC.md §Settings keys.
- SPEC.md §Settings gains `equaliser/user/<name>`, one key per user preset
  holding eleven values — ten band gains then the preamp.
- BUG-006 recorded against GStreamer: `equalizer-nbands` advertises its band
  gains as controllable and accepts a control binding, but never calls
  `gst_object_sync_values` on them while streaming, so the documented way to
  ramp a gain attaches without error and does nothing. Worked around by driving
  the interpolation from the application.

### Fixed
- BUG-005: the headroom rule attenuated by preamp plus the largest single band
  gain, assuming the cascade could not exceed it. Ten peaking sections multiply
  where they overlap: all ten at +12 dB peak at +21.4 dB near 607 Hz, and the
  measured output clipped at +8.04 dBFS. The rule now attenuates by the
  cascade's computed peak magnitude response; the same curve now peaks at
  −3.65 dBFS.
- BUG-004: SPEC.md named `equalizer-10bands`, whose centre frequencies are
  fixed at 29 Hz–15 kHz and cannot be Winamp's, so it could not satisfy D-007.
  Replaced with `equalizer-nbands` and ten child bands.
- BUG-003: the balance control used a constant-power law intended for panning
  a mono source, which attenuated centred playback by 3.01 dB on both channels
  and made a channel louder when the control moved off centre. Replaced with
  an attenuate-only law; SPEC.md §Volume taper corrected, rationale and all.
- BUG-002: SPEC.md specified a balance law but no element to compute it.
  `audiomixmatrix` is now named in SPEC.md §Pipeline and ARCHITECTURE.md
  §Data flow, placed after the equaliser and before `level` and `spectrum` so
  the meters show balance as heard.
- BUG-001: the documented Qt minimum was unattainable on the reference
  platform, and SPEC.md's Handjet axis values required Qt 6.7. Minimum
  corrected to 6.4; Handjet is now specified as a static instance generated
  with `fonttools varLib.instancer`, removing the version requirement.

### Changed
- F-040 gains two acceptance criteria from the original design brief: shuffle
  and repeat as physical toggle switches readable by position rather than
  cycling buttons, and hairline panel gaps specified in device-independent
  units (AV-005).
- Both gain laws extracted as pure static functions on `core/Engine` and
  covered by unit checks in the acceptance harness, which now runs 34 checks.
- D-012's consequence claiming Handjet's axes are token data amended in place;
  it no longer holds under the static-instance resolution. The decision stands.
- BUILD.md rewritten from the first successful build, per ROADMAP.md Phase 1.
  The stated Qt minimum drops from 6.5 to 6.4: Ubuntu 24.04 and Linux Mint 22
  ship 6.4.2 and offer nothing later, so the reference platform could not meet
  the previous figure (BUG-001).
- `readout-floor` token role extended to cover the unlit-segment ghost
  layer in addition to the lowest active meter segment (D-012).

### Notes
- No source code, build system or `LICENSE` file yet. D-010 blocks the
  first public commit of source.
- Font files are specified but not present. Bundling them into
  `resources/fonts/` waits on D-010 for the same reason.
- The seven fonts gathered before D-012 were discarded unused: five
  were non-commercial freeware, one personal-use only, one a demo cut
  of a commercial family, and one forbade distribution within a
  compilation. None was ever committed.
