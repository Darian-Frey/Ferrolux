# Roadmap

Phased development plan for Ferrolux RS-1. Phases are append-only; mark Complete with an ISO date rather than deleting.

The ordering principle is that audio correctness comes before appearance. The panel is the point of the project, but a beautiful panel over a pipeline that stutters is not shippable, and meter work is meaningless until there is a signal to meter.

---

## Phase 1 — Transport core
**Goal:** Play a file correctly from a placeholder interface.
**Status:** Complete 2026-09-02
**Features delivered:** F-001, F-002, F-003, F-004
**Deliverables:**
- [x] CMake project skeleton with Qt6 and GStreamer discovery
- [x] `core/Engine` wrapping a `playbin3` pipeline with a documented state machine
- [x] Position and duration reporting on a single per-frame poll
- [x] Volume with cubic taper, balance with constant-power pan
- [x] Throwaway QML harness with five buttons and a position bar
- [x] `BUILD.md` written the moment the first build succeeds

**Acceptance:** A FLAC and a VBR MP3 both play end to end, seek accurately, pause and resume without artefacts, and survive being stopped and restarted twenty times without a leak or a hung pipeline.

**Acceptance met** on 2026-09-02 by `tests/acceptance_transport`, which drives the engine headlessly through every clause: twenty-six checks across the two files plus the twenty stop-start cycles, all passing. Seek error measured at 0 ms on both files against a 500 ms tolerance; resident growth across the cycles was 976 kB.

Two things the phase surfaced that were not anticipated. BUG-001 records that the documented Qt minimum was unattainable and that SPEC.md's Handjet axes need Qt 6.7. BUG-002 records that SPEC.md specifies a balance law but no pipeline element to compute it — a gap filled provisionally during implementation and still needing a ruling.

---

## Phase 2 — Playlist
**Goal:** Manage a real listening session.
**Status:** Not started
**Features delivered:** F-010, F-011, F-012, F-013, F-014, F-005
**Deliverables:**
- [ ] `library/PlaylistModel` as a `QAbstractListModel` with asynchronous metadata population
- [ ] TagLib integration on a worker thread
- [ ] Multi-select, drag reorder, remove, clear, single-level undo
- [ ] Shuffle as a permutation; repeat modes
- [ ] M3U/M3U8/PLS load and save
- [ ] Sort and live filter
- [ ] Gapless advance via `about-to-finish`

**Acceptance:** A 20,000-entry playlist loads, scrolls at 60 fps, sorts in under a second, and survives a full shuffle pass with no repeats before exhaustion. A known-gapless album plays through with no audible join.

---

## Phase 3 — Equaliser
**Goal:** Ten bands that sound right and behave under abuse.
**Status:** Not started
**Features delivered:** F-020, F-021, F-022
**Deliverables:**
- [ ] `core/Equaliser` abstraction with the GStreamer `equalizer-10bands` backend behind it
- [ ] Band centres and gain range per SPEC.md §Equaliser
- [ ] Preamp with headroom management
- [ ] Bypass verified bit-identical
- [ ] Built-in preset bank and user preset storage
- [ ] Winamp `.eqf` import

**Acceptance:** All ten bands at +12 dB simultaneously, on a loud master, produces no clipping and no denormal stalls. Rapidly dragging a slider produces no zipper noise. Bypass output matches the source byte for byte.

---

## Phase 4 — Meters
**Goal:** Displays that are worth looking at.
**Status:** Not started
**Features delivered:** F-030, F-031, F-032, F-033
**Deliverables:**
- [ ] `meters/MeterSource` consuming `level` and `spectrum` bus messages
- [ ] Ballistics, smoothing and peak-hold on the CPU per SPEC.md §Meters
- [ ] `meters/MeterTexture` — an `Nx1` RG16 texture item exposed to QML
- [ ] Spectrum bar and mirrored-spectrum shaders with logarithmic mapping
- [ ] VU needle shader with correct integration time
- [ ] LED peak ladder shader
- [ ] Mode cycling on click, persisted

**Acceptance:** All four modes hold 60 fps at 3840×2160 on the reference hardware with a headroom margin of at least 30% of the frame budget. The VU needle is visually indistinguishable from a reference deck when both are fed the same programme material.

---

## Phase 5 — The panel
**Goal:** The reason the project exists.
**Status:** Not started
**Features delivered:** F-040, F-041, F-042
**Deliverables:**
- [ ] Design token set: palette, radii, bevel geometry, typography
- [ ] Panel chrome, bezels and control surfaces as QML components
- [ ] Transport buttons with press travel and tactile timing
- [ ] Playlist and equaliser sections styled to match
- [ ] Compact mode
- [ ] Verification pass at 1×, 1.5×, 2× and 3× device pixel ratio

**Acceptance:** Screenshots at every tested scale factor are pixel-crisp with no resampling. A viewer shown the panel without context reads it as photographed hardware rather than a rendered interface.

---

## Phase 6 — Desktop integration and polish
**Goal:** Behaves like a citizen of the desktop.
**Status:** Not started
**Features delivered:** F-015, F-043, F-050, F-051, F-052
**Deliverables:**
- [ ] MPRIS2 service
- [ ] Media key handling under X11 and Wayland
- [ ] Single-instance with enqueue semantics and CLI arguments
- [ ] Session restore
- [ ] Full keyboard control with an in-app shortcut reference
- [ ] Desktop entry, icon set, MIME associations

**Acceptance:** Playback controls work from the desktop shell and lock screen. Opening a file from the file manager enqueues into a running instance. Closing and reopening restores the session exactly.

---

## Phase 7 — RS-1 release
**Goal:** Something other people can install.
**Status:** Not started
**Features delivered:** — (release engineering, no new capabilities)
**Deliverables:**
- [ ] Licence settled and `LICENSE` committed (D-010)
- [ ] All Must-priority features Complete or explicitly Withdrawn
- [ ] Every `ATTACK_VECTORS.md` entry at Critical severity has implemented detection
- [ ] `BENCHMARKS.md` created with baseline numbers from Phase 4 and Phase 5 measurement
- [ ] Packaging: Flatpak, and a `.deb` for Debian and Ubuntu derivatives
- [ ] `CHANGELOG.md` release section, tagged `v1.0.0`, badged RS-1

**Acceptance:** A clean install on a machine with no development toolchain plays music, and a second person can follow `BUILD.md` from a bare checkout to a running binary without asking a question.

---

## Deliberately unscheduled

Candidate features in `FEATURES.md` are not assigned phases. They enter the roadmap only when a decision is recorded promoting them, which forces the scope question to be answered explicitly rather than by accretion.
