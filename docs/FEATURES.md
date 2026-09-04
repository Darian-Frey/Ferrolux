# Features

Capability list for Ferrolux RS-1. Stable IDs are append-only; withdrawn features keep their ID and gain `Status: Withdrawn`.

Priorities follow MoSCoW: Must / Should / Could / Won't.

## Target users

People who keep a local music library on Linux and want a player with the character of physical hardware rather than a flat modern interface. Secondarily, anyone who has tried to run a classic Winamp-style skinned player on a high-DPI display and found the result unusable.

---

## Playback

### F-001 Local file playback
**Priority:** Must
**Acceptance:**
- Plays FLAC, MP3, Ogg Vorbis, Opus, AAC/M4A, WAV, AIFF, WavPack, Musepack and ALAC from local paths
- Unsupported or corrupt files produce a visible error and advance the playlist rather than stalling
**Status:** Partial (Phase 1, 2026-09-02) — FLAC and VBR MP3 verified end to end. The other eight formats in the acceptance list are unverified, and the clause about advancing the playlist cannot be satisfied until F-010 exists.
**Notes:** Format coverage is delegated to GStreamer plugin sets — see D-002.

### F-002 Transport controls
**Priority:** Must
**Acceptance:**
- Play, pause, stop, previous, next respond within 100 ms of input
- Stop resets position to zero; pause preserves it
- Previous within the first 3 seconds of a track goes to the previous track, otherwise restarts the current one
**Status:** Partial (Phase 1, 2026-09-02) — play, pause and stop verified, including stop resetting position to zero and pause preserving it. Previous restarts the current track outside the three-second window and otherwise emits `previousTrackRequested()`, because play order belongs to the playlist model (invariant 5). Next has no meaning until F-012 and is wired to stop in the harness.

### F-003 Seeking
**Priority:** Must
**Acceptance:**
- Dragging the position bar seeks with audible scrub feedback suppressed
- Seeking in a VBR MP3 lands within 500 ms of the target
- Position display updates at most once per rendered frame — see AV-002
**Status:** Complete (Phase 1, 2026-09-02) — seek error measured at 0 ms on both FLAC and VBR MP3 against the 500 ms tolerance. Seeks are flushing and accurate; the harness seeks on slider release only, and a single `FrameAnimation` is the only position poller.

### F-004 Volume and balance
**Priority:** Must
**Acceptance:**
- Volume follows a cubic taper, not linear amplitude
- Balance is a constant-power pan across the stereo field
- Both persist across restart
**Status:** Partial (Phase 1, 2026-09-02) — cubic taper and constant-power pan implemented per SPEC.md §Volume taper and verified. Balance is an `audiomixmatrix` diagonal whose placement in the pipeline is unspecified; see BUG-002. Persistence verified by hand on 2026-09-02: a clean exit writes `playback/volume` and `playback/balance` to `~/.config/ferrolux/ferrolux.ini`, matching SPEC.md §Settings. It is not covered by the acceptance harness, which runs headless and never quits through `aboutToQuit`.

### F-005 Gapless playback
**Priority:** Should
**Acceptance:**
- Consecutive tracks from a gapless album play with no audible discontinuity at the join
- Verified against a known-gapless reference recording
**Status:** Partial (Phase 2, 2026-09-02) — the handover mechanism is implemented and verified: `about-to-finish` swaps the URI on the streaming thread from a pre-cached value, the pipeline never returns to Stopped across the join, and the playlist follows without re-loading the source. The audible clause is **not** verified; it needs a real gapless recording and a capture of the sink, per AV-006.
**Notes:** Depends on `playbin3`'s about-to-finish handling; see AV-006.

### F-006 ReplayGain
**Priority:** Could
**Acceptance:**
- Track and album gain tags applied when present, selectable between the two modes
- Configurable pre-amp and clipping prevention
**Status:** Not started (candidate)

---

## Playlist

### F-010 Playlist model
**Priority:** Must
**Acceptance:**
- Add individual files, whole folders (recursively), or a drag-and-drop selection from a file manager
- Holds 20,000 entries without the UI dropping below 60 fps during scroll — see AV-008
- Metadata read asynchronously; rows populate progressively rather than blocking the add
**Status:** Complete (Phase 2, 2026-09-02) — 20,000 entries add in 17 ms with tags read on a private worker pool. Individual files, whole folders and a drag-and-drop selection all route through `addPaths`, which expands directories recursively, keeps only audio suffixes and sorts the result. Scroll frame time is not yet measured; see ROADMAP.md Phase 2.

### F-011 Playlist editing
**Priority:** Must
**Acceptance:**
- Multi-select with modifier keys, remove selection, clear all
- Drag to reorder, including multi-row drags
- Undo for the last destructive operation
**Status:** Complete (Phase 2, 2026-09-02) — multi-select with Ctrl and Shift, remove, clear, and single-level undo of both. Drag reorder handles a non-contiguous selection: it collapses into one contiguous block at the drop point, preserving the selected rows' relative order, and the current entry and any shuffle permutation survive the move. The permutation is tested independently of the interaction that drives it. Reordering is disabled while a filter is active, because a drop position between two visible rows is ambiguous when rows are hidden between them.

### F-012 Play order
**Priority:** Must
**Acceptance:**
- Shuffle produces a permutation without repeats until the list is exhausted, not independent random picks
- Repeat modes: off, repeat-all, repeat-one
- Shuffle state survives track changes and is not recomputed on each advance
**Status:** Complete (Phase 2, 2026-09-02) — order is a held permutation with a cursor into it. Verified: a pass visits every entry exactly once, `nextRow()` is stable across repeated calls, and a second pass under repeat-all is a fresh permutation.

### F-013 Playlist file I/O
**Priority:** Should
**Acceptance:**
- Load and save M3U, M3U8 and PLS
- Relative paths preserved on save when the playlist file sits above the media
**Status:** Complete (Phase 2, 2026-09-02) — M3U, M3U8 and PLS round-trip URLs and durations including `-1`. Read tolerantly: unknown directives, absent headers, CRLF and out-of-order PLS keys are all skipped rather than fatal.

### F-014 Sort and filter
**Priority:** Should
**Acceptance:**
- Sort by title, artist, album, duration, path or file date
- Live text filter narrows the visible rows without altering play order
**Status:** Sort complete (Phase 2, 2026-09-02). Sort permutes the model and so changes play order; the filter is a proxy and provably does not, which is asserted directly. Filtering 20,000 rows takes under a millisecond.

**The live filter has no interface as of 2026-09-04.** Its field was removed from the file toolbar at the author's request. `PlaylistFilter` is unchanged, still the model the list is bound to, and still tested — what is gone is the control that set `filterText`, so the acceptance clause above cannot currently be exercised by a user. Recorded rather than quietly left as Complete: a feature reachable only from a test is not a feature the player has. `qml/EntryField.qml` is kept for the same reason — it is the interface this clause will want back, wherever it ends up.

### F-015 Session restore
**Priority:** Should
**Acceptance:**
- Playlist contents, current track, playback position and play order restored on next launch
**Status:** Not started (Phase 6)

---

## Equaliser

### F-020 Ten-band graphic equaliser
**Priority:** Must
**Acceptance:**
- Ten bands at the centre frequencies in SPEC.md §Equaliser, each adjustable ±12 dB
- Adjusting a band takes effect without a pipeline restart and without an audible click
- Bypass toggle returns bit-identical output to the unprocessed signal
**Status:** Partial (Phase 3, 2026-09-02) — ten bands at Winamp's centres, ±12 dB, adjusted by property write with no pipeline restart. Bypass is verified **bit-identical** against a captured reference, with a control check confirming the element is genuinely in circuit. Gains ramp over 30 ms rather than jumping: verified to interpolate linearly, to land exactly on target, and to start no ramp for a change that changes nothing. The *audible* absence of zipper noise is not measured; see ROADMAP.md Phase 3.

### F-021 Preamp
**Priority:** Must
**Acceptance:**
- ±12 dB preamp applied ahead of the band filters
- Combined preamp and band gain cannot drive the output into hard clipping — see AV-003
**Status:** Complete (Phase 3, 2026-09-02) — ±12 dB preamp ahead of the band filters. The combined gain path is measured and reported rather than attenuated: an earlier design subtracted it and thereby cancelled every boost, making the equaliser cut-only (BUG-008). Verified against the worst case: ten bands at +12 dB with a +12 dB preamp measures 32.04 dB of gain against 35.69 dB reported, with nothing non-finite — the figure bounds reality and the filters stay stable, which is what AV-003 actually requires.

### F-022 Presets
**Priority:** Should
**Acceptance:**
- Built-in preset bank matching the classic named curves
- User presets saved and recalled by name
- Import of Winamp `.eqf` preset files
**Status:** Partial (Phase 3, 2026-09-02) — the `.eqf` decoder is complete and tested, including the inverted scale where byte 0 is maximum boost and 63 maximum cut, and both the bare eleven-byte payload and the full library file with header and name. A built-in bank of nine curves recalls by name. User presets save and recall by name, shadowing a built-in of the same name, with names containing a slash rejected because they would open a settings subgroup rather than name a preset. Both the bank and the importer are reachable from the harness.
**Notes:** The built-in curves are Ferrolux's own shaping, not byte copies of Winamp's bank. D-006 already establishes that the filter response differs from Winamp's, so reproducing its exact numbers would imply a fidelity that does not exist. Importing a real `.eqf` is the exact path.

### F-023 Per-track equaliser settings
**Priority:** Could
**Acceptance:**
- A curve can be bound to a specific file or album and applied automatically on load
**Status:** Not started (candidate)

---

## Displays

### F-030 Meter data acquisition
**Priority:** Must
**Acceptance:**
- Per-channel RMS and peak, and banded spectrum magnitudes, delivered at the interval in SPEC.md §Meters
- Acquisition never blocks the GStreamer streaming thread — see AV-001
**Status:** Not started (Phase 4)

### F-031 Shader-rendered spectrum display
**Priority:** Must
**Acceptance:**
- Logarithmic frequency mapping with no visible bin collapse below 200 Hz — see AV-011
- Peak-hold caps with configurable decay
- Holds 60 fps at 4K on the reference hardware in BUILD.md
**Status:** Partial (Phase 4, 2026-09-02) — bars and the mirrored variant drawn per fragment from the meter texture, with antialiased edges resolved by smoothstep over one fragment rather than by a hard comparison, and peak-hold caps that go dark at rest. Bass spreads across many bands rather than collapsing; the four lowest of 24 are interpolated and SPEC.md §Meters records the consequence. Outstanding: the 60 fps measurement, which needs AV-002's instrumentation.

### F-032 Shader-rendered VU display
**Priority:** Must
**Acceptance:**
- Needle ballistics match the integration time in SPEC.md §Meters, not instantaneous RMS
- Separate faster peak indicator alongside the needle
- Face, scale marks and needle share one antialiasing model
**Status:** Partial (Phase 4, 2026-09-02) — face, arc, tick marks, tapered needle, hub and peak lamp are all distance fields resolved by the same smoothstep over one fragment, so they share an antialiasing model by construction rather than by matching two techniques. Ballistics are not in the shader: deflection arrives already integrated from `MeterSource`, per D-005, measured at 302.0 ms to 99% with 1.16% overshoot. Outstanding: the comparison against a reference deck, which is a judgement rather than a measurement.

### F-033 Switchable display modes
**Priority:** Must
**Acceptance:**
- Cycle between spectrum bars, mirrored spectrum, stereo VU needles and LED peak ladder by clicking the display
- Selection persists across restart
- Switching does not interrupt audio or drop a frame
**Status:** Partial (Phase 4, 2026-09-02) — all four modes read the same texture and the same meter source, so switching hides one shader and shows another and cannot touch the pipeline. Clicking the display cycles; the choice persists under the SPEC.md §Settings `meters/mode` key. Outstanding: the dropped-frame claim, which needs AV-002's instrumentation to assert rather than assume.

### F-035 Flame spectrum mode
**Priority:** Could
**Acceptance:**
- The same band data as the bar display, read as a continuous curve rather than as discrete bars
- Shaded in contour steps rather than a smooth gradient
**Status:** Complete (Phase 4, 2026-09-02)
**Notes:** Added at the author's request during Phase 4, and recorded here rather than left as an undocumented fifth mode. It costs nothing structurally: it reads the existing texture and the existing band data, and it is the one mode that spends what D-004 bought — sampling *between* band centres so linear filtering interpolates the silhouette for free. The bar display deliberately samples only at centres, because bars want one flat value each, which is why both read the same texture happily.

The contour steps are the point rather than a stylisation. A continuous gradient reads as an airbrush; the steps read as flame, because a real flame's luminosity falls off in visible layers.

### F-034 Oscilloscope mode
**Priority:** Could
**Acceptance:**
- Time-domain waveform display fed from a raw PCM tap
**Status:** Not started (candidate)
**Notes:** Needs a `tee` into an `appsink`; the level/spectrum path does not carry PCM. See ARCHITECTURE.md §Data flow.

---

## Interface

### F-040 Cassette futurism panel
**Priority:** Must
**Acceptance:**
- Transport, display, playlist and equaliser sections present in one window
- Warm off-white shell, amber readouts, chunky moulded controls with visible travel state
- Shuffle and repeat are physical toggle switches whose state is readable from the switch position alone, without a text label. They are a different class of control from the momentary transport buttons and must not be cycling buttons — the Phase 2 harness uses cycling buttons and is exactly what this criterion excludes
- Hairline gaps between panel sections, specified in device-independent units. A hairline given in pixels is the founding defect of the project in miniature — see AV-005
- Values are lit, legends are printed. Anything that reports a *value* — elapsed and remaining time, volume, balance position, equaliser gains — is rendered as an illuminated readout in the `readout` palette using the readout faces. Anything that *names* a control — band centre frequencies, `pre`, section titles, button legends — is silkscreened on the chassis in `ink`. The two must not be confused: a lit legend implies a state it does not have, and a printed value cannot change
- Continuous controls carry a legible scale, and balance has a detent at centre. Centre is the position a balance control returns to most and the one position it cannot be set to by eye; a control that can be left imperceptibly off-centre with no way to see it is the defect, not the user's aim
- All chrome drawn as vectors or shaders; no fixed-size bitmap assets in the control surface
**Status:** Not started (Phase 5)
**Notes:** The toggle-switch and hairline criteria come from the original design brief accompanying the mockup, recorded here on 2026-09-02 because they were otherwise carried only in that prose. The same brief is the source of the amber-or-VFD-green readout option in F-044 and the needle VU meters in F-032.

### F-041 Resolution independence
**Priority:** Must
**Acceptance:**
- Correct at 1× through 3× device pixel ratio with no resampling artefacts
- Window resizes continuously rather than snapping to fixed multiples — see AV-005
**Status:** **Met** 2026-09-03, measured by `tools/verify-scaling.sh` rather than asserted. Edge rise holds at one device pixel from 1× to 3×, and each capture reduces back to the 1× panel instead of a differently laid-out one.

### F-042 Compact mode
**Priority:** Should
**Acceptance:**
- Collapses to a single transport-and-display strip, equivalent to Winamp's window shade. The strip carries volume as well as the transport: it is the control reached for most often while listening, and a shade that cannot change the volume sends you back to the full panel for the one thing you folded it away to stop needing
- Toggling preserves playback state and window position
**Status:** **Met** 2026-09-04. Driven under XTest rather than judged by eye: 780 to 175 to 780, window position identical at all three points, and the engine still playing across both toggles. Playback survives by construction — the folded sections are hidden, not unloaded, and nothing in the fold reaches the engine.

### F-043 Keyboard control
**Priority:** Should
**Acceptance:**
- Full transport, playlist navigation and equaliser reachable from the keyboard
- Shortcuts discoverable from an in-app reference
**Status:** Not started (Phase 6)

### F-044 Theme variants
**Priority:** Could
**Acceptance:**
- Alternative panel finishes (brushed aluminium, black anodised, VFD-green readout) selected from settings
- Variants are token sets over the same geometry, not separate asset packs
**Status:** **Met** 2026-09-04 for the finishes. Four sets ship — `ferric`, `anodised`, `glacier`, `ember` — chosen from the settings drawer and exchanged while the panel is running. Each is the same file with a different palette, so the geometry and the type scale are shared by construction rather than copied and kept in step. `tests/tokens_test` holds every set to the same vocabulary and to contrast floors, which is BUG-020 turned into a check.

The variants change the *chassis* and keep the amber lamp, which is how real equipment varies — the paint is the paint and the lamp is the lamp. A **VFD-green readout** is the remaining clause and is blocked by IMP-006: four pale tints in the meter shaders are still literal, and they are lamp-family, so a set that changed the lamp would change everything except the meters.

---

## Desktop integration

### F-050 MPRIS2
**Priority:** Should
**Acceptance:**
- Exposes playback state, metadata and transport actions over D-Bus
- Desktop media controls and lock-screen widgets operate the player correctly
**Status:** Not started (Phase 6)

### F-051 Media key handling
**Priority:** Should
**Acceptance:**
- Hardware media keys work under both X11 and Wayland
**Status:** Not started (Phase 6)
**Notes:** Under Wayland this is delegated to MPRIS rather than a global grab.

### F-052 Single instance and CLI
**Priority:** Should
**Acceptance:**
- A second launch with file arguments enqueues into the running instance
- `--enqueue`, `--play`, `--replace` argument forms supported
- Without a flag, paths fill the playlist and select the first entry without starting playback
**Status:** Not started (Phase 6)
**Notes:** The bare default is recorded above rather than left implied. It was implied once, drifted to auto-play unnoticed during Phase 2, and had to be found by use — see BUG-015. An explicit `--play` only means something if the default is not it.

---

## Out of scope

Ferrolux RS-1 will not do these things. Each is a deliberate exclusion, not an oversight.

- **Winamp classic skin compatibility.** The whole reason the project exists is that fixed-size bitmap skins cannot scale. Supporting them would reintroduce the defect. See D-003.
- **Streaming service integration.** No Spotify, Tidal, Subsonic or equivalent. Local files and local network shares only.
- **Video playback.** Audio only, regardless of what the underlying pipeline could do.
- **A media library database.** Ferrolux manages a playlist, not a catalogued collection. No scanning daemon, no library schema, no artist/album browser.
- **Tag editing.** Metadata is read, never written.
- **Internet radio.** Deferred rather than refused — see candidates below.
- **A visualisation plugin API.** Display modes ship with the application; there is no third-party extension surface in RS-1.

## Future / candidate features

Not committed to, recorded so the ideas are not lost.

- Internet radio stream URLs with ICY metadata
- CD playback and ripping
- Crossfade between tracks with configurable curve
- A second window mode for a wide, rack-unit-shaped layout
- Output device selection with per-device EQ profiles
- Parametric equaliser as an alternative to the ten-band graphic
- Spectrogram (waterfall) display mode
- A hardware companion build, reusing the panel design on a physical device
