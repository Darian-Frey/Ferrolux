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
**Status:** Feature-complete 2026-09-02; two acceptance clauses unverified
**Features delivered:** F-010, F-011, F-012, F-013, F-014, F-005
**Deliverables:**
- [x] `library/PlaylistModel` as a `QAbstractListModel` with asynchronous metadata population
- [x] TagLib integration on a worker thread
- [x] Multi-select, drag reorder, remove, clear, single-level undo
- [x] Shuffle as a permutation; repeat modes
- [x] M3U/M3U8/PLS load and save
- [x] Sort and live filter
- [x] Gapless advance via `about-to-finish`

**Measured against the acceptance criterion.** On 20,000 entries: add 17 ms,
sort 52 ms, shuffle 1 ms, remove 6 ms, filter 0 ms, against a one-second budget.
A full shuffle pass visits every entry exactly once with no repeats, and a
second pass under repeat-all is a fresh permutation. 80 checks across
`playlist_model_test` and `metadata_reader_test`, plus 9 for the gapless
handover in `acceptance_transport`.

**Two clauses remain unverified**, and the phase is not marked Complete until
they are:

- *"scrolls at 60 fps"* — the list is virtualised and the data is resident, but
  no frame-time measurement has been taken. The instrumentation AV-002 needed
  now exists (`FrameTimer`, built in Phase 4) and applies to the whole window,
  so it can measure this; what is missing is a way to drive a 20,000-entry
  scroll under it, which is a test harness rather than a new measurement.
- *"a known-gapless album plays through with no audible join"* — the handover
  mechanism is verified (the pipeline never returns to Stopped, the playlist
  follows without re-loading), but the audible half needs a real gapless
  recording and a capture of the sink output, per AV-006.

**Acceptance:** A 20,000-entry playlist loads, scrolls at 60 fps, sorts in under a second, and survives a full shuffle pass with no repeats before exhaustion. A known-gapless album plays through with no audible join.

---

## Phase 3 — Equaliser
**Goal:** Ten bands that sound right and behave under abuse.
**Status:** Feature-complete 2026-09-02; the audible clauses unverified
**Features delivered:** F-020, F-021, F-022
**Deliverables:**
- [x] `core/Equaliser` abstraction with a stock GStreamer backend behind it — `equalizer-nbands`, not `equalizer-10bands`; see BUG-004
- [x] Band centres and gain range per SPEC.md §Equaliser
- [x] Preamp with headroom management
- [x] Bypass verified bit-identical
- [x] Built-in preset bank and user preset storage
- [x] Winamp `.eqf` import
- [x] Gain ramp to prevent zipper noise on a slider drag
- [x] Equaliser controls in the harness

**Two defects found by building it.** BUG-004: SPEC.md named an element whose
centre frequencies are fixed and cannot be Winamp's, so it could not satisfy
D-007. BUG-005 is the significant one — the headroom rule attenuated by the
largest single band gain, which under-attenuated by 9 dB and clipped the output
by 8 dB under the exact condition AV-003 describes. Both are fixed, and AV-003's
detection is now implemented rather than aspirational: it is the check that
found BUG-005 on its first run. BUG-006 records a third, upstream: the element
advertises controllable band gains and never honours them, which makes the
obvious way to implement the ramp fail silently.

**Two clauses of the acceptance criterion remain unverified**, in the same sense
as Phase 2's:

- *"no denormal stalls"* — the worst-case capture asserts every sample is
  finite, which catches instability, but a denormal stall shows up as CPU cost
  rather than as a bad sample and needs timing instrumentation to see.
- *"rapidly dragging a slider produces no zipper noise"* — the ramp is verified
  to interpolate linearly, reach exactly its target and start no ramp for a
  change that changes nothing, but *audible* absence of zipper noise is not
  measured. Doing so needs a capture with a gain change mid-stream and a
  discontinuity threshold nobody has yet had to defend.

**Acceptance:** All ten bands at +12 dB simultaneously, on a loud master, produces no clipping and no denormal stalls. Rapidly dragging a slider produces no zipper noise. Bypass output matches the source byte for byte.

---

## Phase 4 — Meters
**Goal:** Displays that are worth looking at.
**Status:** Complete 2026-09-03
**Features delivered:** F-030, F-031, F-032, F-033, F-035
**Deliverables:**
- [x] `meters/MeterSource` consuming `level` and `spectrum` bus messages
- [x] Ballistics, smoothing and peak-hold on the CPU per SPEC.md §Meters
- [x] `meters/MeterTexture` — an `Nx1` texture item exposed to QML; the layout diverges from RG16 and SPEC.md §Meters records why
- [x] Spectrum bar and mirrored-spectrum shaders with logarithmic mapping
- [x] VU needle shader with correct integration time
- [x] LED peak ladder shader
- [x] Mode cycling on click, persisted
- [x] Flame mode (F-035), added during the phase at the author's request
- [x] Rest on stop, hold on pause
- [x] Frame-time instrumentation for AV-002 — `FrameTimer` plus `tools/measure-frames.sh`; see the acceptance note below for what it does not cover
- [x] A `QQuickRenderControl` harness rendering to an offscreen texture on the OpenGL RHI — `tests/frame_bench`, which closes the 3840×2160 and 30% headroom clauses. It found flame running at 37 fps at 4K on its first run; see BUG-016

**Five defects found by building it.** BUG-016 is the one the phase was
shaped to find: the flame display ran at 37 fps at 3840x2160, and the first
benchmark written to catch it hid the failure by feeding a loud signal. BUG-010: SPEC.md specified a first-order
VU with a 1% to 1.5% overshoot, and a first-order system cannot overshoot at all
— the IEC characteristic is second-order, now solved and implemented. BUG-011 is
the significant one: the analysis elements sit upstream of the sink, so their
messages arrive a measured 1307 ms ahead of the audio they describe, and a meter
fed on arrival would show a transient more than a second early. BUG-012: closing
the window left the process alive for ever, because `gst_deinit()` ran while a
live pipeline still existed. BUG-013: the VU readout never moved, because a QML
binding over a plain method call has nothing to watch.

The texture's alpha channel is now spoken for as a constraint rather than as
spare capacity: putting data in it corrupts every mode at once, because the
scene graph premultiplies on upload. SPEC.md §Meters records it as a rule.

AV-011 is also quantified rather than feared: at 44.1 kHz, 4 of the 24 display
bands and 12 of the 48 span less than one analysis bin, so they borrow the
nearest and the lowest bars move in lockstep. That is recorded in SPEC.md as an
accepted consequence — bars moving together are honest about the analysis
resolution, whereas a bar stuck at silence would read as missing bass.

**Acceptance:** All four modes hold 60 fps at 3840×2160 on the reference hardware with a headroom margin of at least 30% of the frame budget. The VU needle is visually indistinguishable from a reference deck when both are fed the same programme material.

> **Met, 2026-09-03**, by `tools/measure-frames.sh`. All five modes hold 60 fps
> with zero late frames at 3840×2160, with between 46% and 64% of the frame
> budget spare — the requirement is 30%. The window pass covers the whole
> application up to the sizes this display can render; the offscreen pass covers
> the meter display at full resolution with a fence after each frame, so its
> headroom figure is real and not a lower bound. AV-002 records what each pass
> does and does not cover.
>
> Getting there cost one substantial defect. Flame ran at **37 fps** at 4K, and
> the benchmark's first version hid it by feeding a loud signal — quiet passages
> turn out to be the expensive case for that shader, not the cheap one. Both the
> shader and the benchmark are fixed; see BUG-016.
>
> The needle clause is met by construction and measured in `tests/meters_test`:
> 302.0 ms to 99% deflection with 1.16% overshoot, against IEC 60268-17's 300 ms
> and 1–1.5%.

---

## Phase 5 — The panel
**Goal:** The reason the project exists.
**Status:** In progress, started 2026-09-03
**Features delivered:** F-040, F-041, F-042
**Deliverables:**
- [x] Design token set: palette, radii, bevel geometry, typography — `resources/themes/ferric.json` loaded by `ui/ThemeTokens`, with the vocabulary spelled once in `qml/Tokens.qml` and scaling applied there and nowhere else. The four faces are bundled under `resources/fonts/` and generated by `tools/make-fonts.sh`; `tests/tokens_test` holds the file to SPEC.md value by value and asserts each face loads under the family the set asks for
- [~] Panel chrome, bezels and control surfaces as QML components — the chassis, `qml/PanelButton.qml` (a moulded face with travel), `qml/Legend.qml` (silkscreen) and `qml/TransportGlyph.qml` (drawn marks) are in. The playlist and equaliser sections still sit on Qt Quick Controls
- [x] Transport buttons with press travel and tactile timing — the face drops into its well and its lighting inverts across the same few tens of milliseconds, both driven by one `depress` quantity
- [ ] Playlist and equaliser sections styled to match
- [ ] Compact mode
- [ ] Verification pass at 1×, 1.5×, 2× and 3× device pixel ratio

**Acceptance:** Screenshots at every tested scale factor are pixel-crisp with no resampling. A viewer shown the panel without context reads it as photographed hardware rather than a rendered interface.

**Two specification faults found by rendering the first readout.** SPEC.md's
Handjet weight was marked provisional pending this phase, and at 500 the face
rendered as continuous strokes rather than as separated dots — failing the
intent stated in the sentence below the table that set it. It is now 300, and
the same render established that the dot-matrix character has a minimum size of
roughly 20 units, which raised `size-readout-large` from the 13 the mockup drew
a proportional face at. SPEC.md also cited an OFL Reserved Font Name clause that
Handjet does not invoke. See BUG-017 and BUG-018.

The chrome costs nothing measurable. Re-running `tools/measure-frames.sh` after
the transport strip — five drawn glyphs and a gradient per control, on every
frame — leaves the window pass at 0.12 to 0.34 ms of CPU render and every mode
still holding at 3840×2160. Chrome is checked against AV-002 as it lands rather
than at the end of the phase, because a panel that has become slow is much
harder to attribute once all of it is there.

The dot-matrix ghost codepoint, left provisional pending this phase, resolves to
**none**: the instance has no full-cell glyph, and SPEC.md is explicit that a
face without one gets no ghost rather than an approximated one. The time readout
has an unlit ghost and the title does not.

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
