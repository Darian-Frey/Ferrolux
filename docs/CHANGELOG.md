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

### Fixed
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
