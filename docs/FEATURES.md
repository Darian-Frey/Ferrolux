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
**Status:** **Met** 2026-09-07 (Phase 6). `platform/Session` writes the contents to a playlist file under `~/.local/share/ferrolux/` and the other three to SPEC.md §Settings. Verified: an album played to 49.9 s on row 2 came back on the next launch as the same 16 entries, the same row, and 49922197 µs — the saved figure to the nanosecond.

**Play order means the permutation, not the shuffle flag.** F-012's acceptance is explicit that shuffle is an order the model *holds* and does not recompute on each advance, so restoring `playback/shuffle` and letting the model reshuffle would perform exactly that recomputation, once, at launch: shuffle would be on, the list would be in *a* random order, every visible symptom would be right, and the tracks coming up would not be the ones that were coming up when the user quit. Nobody would ever be able to say why. The permutation is therefore saved whole and adopted whole — verified by comparing the stored order across a quit and a restart, byte for byte identical, where a reshuffle of sixteen entries would almost certainly differ.

**Nothing resumes playing.** The position is restored so that pressing Play continues where it left off, and that is as far as it goes; a player that starts on launch because it was playing when it closed makes noise in a quiet room. Confirmed: the restored state reports `Paused`, never `Playing`.

Three cases were driven rather than reasoned about. Paths on the command line are added to the restored session rather than instead of it, and suppress the position seek — resuming into a position belonging to a track the user did not ask for is worse than starting theirs at zero. A play order that does not describe the playlist beside it is refused, with the contents kept and the order rebuilt, because an order indexing entries that are not there is a crash rather than a wrong order. And clearing the playlist removes the keys and the file, so emptying it on purpose is not undone by the next launch.

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
**Status:** **Met** (Phase 4, 2026-09-03). `level` and `spectrum` deliver per-channel RMS and peak and banded magnitudes at the interval SPEC.md §Meters states, and `src/meters/MeterSource` buckets them, applies the ballistics and holds the scheduling queue that corrects the message lead — which is BUG-011, the meters running over a second ahead of the audio.

The second criterion is met by construction rather than by care: both elements post their results as bus messages, and `gst_bus_add_watch` dispatches on the main loop, so no application code runs on a streaming thread at all. That is the strongest form the claim can take, and it is still not a measurement — **AV-001's detection remains unimplemented**, so the invariant is held by review on any change touching `core/` or `meters/`. The status is Met on the criteria as written; AV-001 is what would let it be asserted rather than reasoned.

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

### F-036 Adjustable display proportions
**Priority:** Could
**Acceptance:**
- Every proportion the meter shaders would otherwise carry as a literal is adjustable, and the change is visible on the running display rather than on the next launch
- The values persist across restart
- Every value is clamped to a range that cannot reach a shader failure mode, on the way in from the controls and from a hand-edited settings file alike
- The proportions are independent of the finish: changing the chassis leaves them as they were
- One action returns all of them to the values the displays shipped with
**Status:** **Met** 2026-09-06 (Phase 5). Nine proportions are the user's: the spectrum's bar gap and cap thickness, the flame's ranks, front height, back height, parallax and softness, and the ladder's segment count and over-reference. `src/ui/VisualSettings` holds them, `qml/SettingsWindow.qml` is where they are set, and SPEC.md §Settings owns the eleven keys they persist under. `tests/meters_test` adds nine checks, one of which loads deliberately out-of-range values and asserts they arrive clamped.

**Every value is clamped in the setter rather than at the point of use**, so the settings file cannot be the one route into the program that skips its own validation. This is not defensive habit: a flame with zero ranks or a ladder with zero segments is a division by zero inside a shader, and a shader does not throw. It draws something wrong sixty times a second, on a display whose whole purpose is to be believed.

**They are deliberately not design tokens, and the distinction is the feature.** A token set is what the panel is *made of* — a finish, per SPEC.md §Design tokens — which is why `tests/tokens_test` holds every set to the same vocabulary. These are what the user has *set the displays to*, and they survive a change of finish because a preference for a coarse ladder is not a preference about paint. Keeping them apart also stops a finish being able to redefine them, which would make choosing a chassis silently change the meaning of a setting someone chose (F-044).

**Ranks is the one with a cost attached.** Each rank is up to five texture taps per pixel, and BUG-016 is what happened when nine of them went unmeasured at 2160p. The control stops at sixteen because that is the shader's own ceiling, above which it silently stops drawing them — a slider that can reach a failure mode is a bug with a handle on it.

**Notes:** The controls live in a window of their own rather than in a panel drawer, and that follows from the acceptance criteria rather than from taste. A drawer pushed the playlist down on every opening and covered the display being adjusted, and a proportion can only be judged by watching the meter move while it changes. Where they live is not part of this feature; that they can be changed at all, and by whom, is.

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
**Status:** **Met** 2026-09-04 (Phase 5), on all seven criteria. The four sections are in one window; `ferric`'s shell is a warm off-white and its readout amber, with the moulded controls dropping into their wells on one `depress` quantity that drives travel and lighting together. Shuffle and repeat are `qml/SlideSwitch.qml`, read from the lever position with the marks printed on the chassis and never lit — the cycling buttons of the Phase 2 harness are gone, which is what the criterion excludes. `Tokens.hairline` is a scaled metric and nothing in the panel is expressed in pixels, which `tools/verify-scaling.sh` holds at four ratios. `qml/Readout.qml` and `qml/Legend.qml` are separate components precisely so that a value cannot be printed or a legend lit; `qml/Slot.qml` prints a tick scale and balance carries a detent at centre. No bitmap assets are in the control surface, which is invariant 6.

F-044 varies the chassis without disturbing this: the finishes change the paint and keep the amber lamp, so the warm off-white shell is `ferric` rather than the only possibility.

Note this is the *feature's* acceptance, all of which is objective. ROADMAP.md Phase 5 carries a second, separate clause — whether a viewer shown the panel without context reads it as photographed hardware — which is a judgement for the author and is why the phase is marked feature-complete rather than complete. That clause is not part of F-040 and does not qualify this status.
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
**Status:** **Met** 2026-09-07 (Phase 6). `qml/Shortcuts.qml` holds every binding and `qml/KeyGuide.qml` prints them.

**One table serves both, and that is the design rather than a convenience.** `KeyGuide` renders the same list `Shortcuts` binds from, so the reference cannot describe a key that does nothing or omit one that does something. The alternative shape — `Shortcut` objects beside a hand-written list of what they do — is one edit away from lying at all times, and a reference that lies is worse than none because it is believed.

Transport is on shortcuts; **the equaliser is reachable because `Slot` became keyboard-operable**, which was the substantial part. Arrow keys move a fader by `step`, Page Up and Page Down by `pageStep`, Home and End to the ends, and every one goes out through `moved` and `released` exactly as a drag does — so the caller's policy (seek on release for the position bar, per BUG-009; set at once for a volume) applies to a keypress without the control knowing which is which. A gain in decibels overrides `step` to 1 so that arrowing lands on the whole numbers the readout prints. Focus is drawn rather than borrowed, like everything else on the chassis.

Verified against a running panel with synthesised keys: Space toggles play and pause; Ctrl+Left and Ctrl+Right move track and Ctrl+Left inside three seconds restarts, so F-002's rule survives the route; Shift+Right seeks five seconds; Ctrl+Up and Ctrl+Down move volume by 0.05; Ctrl+E switches the equaliser in and out; Ctrl+H toggles shuffle; Ctrl+K folds the panel from 780 px to 206 and back; F1 and Ctrl+, open the guide and the settings; Ctrl+Q exits cleanly. In the playlist, arrows move the selection with the modifier rules of a click, Home, End and Page Up/Down jump, Return plays and Delete removes. Tab cycles the faders and the ring follows.

**Bare letters are deliberately unused.** A `Shortcut` at window scope fires while a text field has focus, so a bare `S` for stop would be a letter that cannot be typed into a preset name. The set is additionally disabled whenever the focused item takes typed characters, so the sequence choice is a second line of defence rather than the only one. The bare arrows are absent for the same class of reason: they belong to whatever has focus.

Two things are honestly imperfect and neither is claimed otherwise. The Tab order follows the item tree rather than the panel's reading order, so it runs volume, playlist, balance, position. And the playlist shows no focus ring of its own — the selected row is lit either way, so there is no way to see whether the list or a fader has the keyboard until a key is pressed.

### F-044 Theme variants
**Priority:** Could
**Acceptance:**
- Alternative panel finishes (brushed aluminium, black anodised, VFD-green readout) selected from settings
- Variants are token sets over the same geometry, not separate asset packs
**Status:** **Met** 2026-09-04 for the finishes. Four sets ship — `ferric`, `anodised`, `glacier`, `ember` — chosen from the settings window and exchanged while the panel is running. Each is the same file with a different palette, so the geometry and the type scale are shared by construction rather than copied and kept in step. `tests/tokens_test` holds every set to the same vocabulary and to contrast floors, which is BUG-020 turned into a check.

The variants change the *chassis* and keep the amber lamp, which is how real equipment varies — the paint is the paint and the lamp is the lamp.

A **VFD-green readout** is no longer blocked: IMP-006 was applied on 2026-09-05, and every colour the displays use is now derived from `readout` rather than written down. Set the lamp to green and the caps, the flame and the over-reference segments follow it, which was checked by doing exactly that — the peak tier tracked the lamp from 36° to 142° while keeping its 3° offset. No such set ships yet; what was missing was the mechanism, and that is there.

---

## Desktop integration

### F-050 MPRIS2
**Priority:** Should
**Acceptance:**
- Exposes playback state, metadata and transport actions over D-Bus
- Desktop media controls and lock-screen widgets operate the player correctly
**Status:** **Met** 2026-09-06 (Phase 6). `platform/MprisService` exports `org.mpris.MediaPlayer2` and `org.mpris.MediaPlayer2.Player` at the specified object path and claims `org.mpris.MediaPlayer2.ferrolux`, qualifying the name with the process id if a first instance already holds it. Every property, every method and both signals were exercised with a bus client: transport, `Seek`, `SetPosition`, `OpenUri`, `Raise`, `Quit`, and writes to `Volume`, `Shuffle` and `LoopStatus`.

**The second criterion is evidenced rather than assumed.** With the player running under Cinnamon, the shell called `GetAll` on both interfaces without being asked — the desktop discovered the player and populated its controls from it. What has not been done is watching a lock screen on every shell, and the entry does not claim it.

**`PropertiesChanged` is the part that is easy to ship broken.** Qt does not emit it for an adaptor's properties — an adaptor is a passive view onto a `Q_PROPERTY` — so a player can be entirely correct to a client that polls and entirely stale to one that subscribes, which is most of them. Every signal is constructed and sent by hand, and `Metadata` is coalesced to one emission per turn of the event loop: a track change arrives as three separate signals, and publishing on the first gave a map holding the new title against the previous track's URL. It settled a moment later, which is what would have made it the kind of defect nobody reports.

`Position` is deliberately not notified, per the specification — it changes continuously and clients extrapolate. What cannot be extrapolated is a jump, so `Engine` gained a `seeked` signal that distinguishes a discontinuity from ordinary progress, and `Seeked` is emitted from it. Without that, a seek made on the panel would leave every lock screen showing a position that never happened.

### F-051 Media key handling
**Priority:** Should
**Acceptance:**
- Hardware media keys work under both X11 and Wayland
**Status:** **Met** 2026-09-06 (Phase 6) on every desktop that has a media-key mechanism, which the note below did not correctly describe. `platform/MediaKeys` registers with the settings daemon; F-050 covers the desktops that route the keys through MPRIS instead. Verified by synthesising real `XF86Audio` keysyms with XTest against a running player: play toggles, next and previous move through the list — previous going back a track rather than restarting, because it was inside F-002's three-second window — and stop stops.

**X11 under a bare window manager was the case left over**, where there is neither a daemon nor an MPRIS consumer. IMP-008 closed it on 2026-09-07: `platform/X11MediaKeys` grabs the four `XF86Audio` keysyms directly, and is reached only when no daemon answered. It was tested in a nested X server with no window manager and no settings daemon — the condition the entry had said to wait for, made rather than waited for — and that testing found two defects that reading could not have: the fallback was unreachable when there was no session bus at all, which is the very case it exists for, and a grab held on IMP-009's activity rule could never be taken, because under a bare window manager nothing focuses the window and nothing plays until a key starts it. A registration handed back goes to another player; a grab handed back goes to nobody, so the grab is held for the run.

Wayland without a daemon cannot be fixed at all, by design — there is no global grab to make, and the compositor decides.

**Notes:** The original note said this would be "delegated to MPRIS rather than a global grab" under Wayland, implying a grab under X11. Both halves are wrong, and the second is the expensive one: **the major desktops do not route media keys through MPRIS on either display server.** GNOME, Cinnamon and MATE run a settings daemon that owns the keys and gives them to whichever application last registered over `org.gnome.SettingsDaemon.MediaKeys` — an interface that predates MPRIS and has outlived several attempts to retire it. Measured rather than assumed: with F-050 running and correct, a synthesised `XF86AudioPlay` left the player exactly where it was. The daemon had the key and nothing had asked it for one. The split is by desktop, not by display server.

Registration is not permanent — the grab belongs to the D-Bus connection and lapses when the daemon restarts — so each daemon name is watched and re-registered. That path is written but not exercised, because exercising it means restarting a live session's settings daemon.

**The keys are claimed on activity rather than on existence** (IMP-009). They are a single global thing only one application can hold, and the daemon gives them to whoever registered last, so a player that registers at startup and holds until it exits takes them from a browser that is actually playing and never gives them back. Ferrolux holds them while its window has focus, and while it is a session that has played and still has something to resume — a pause counts, because pause is the state a player is left in when the user means to come back. Idle and unfocused, it hands them back.

### F-052 Single instance and CLI
**Priority:** Should
**Acceptance:**
- A second launch with file arguments enqueues into the running instance
- `--enqueue`, `--play`, `--replace` argument forms supported
- Without a flag, paths fill the playlist and select the first entry without starting playback
**Status:** **Met** 2026-09-06 (Phase 6). `platform/CommandLine` parses the forms and `platform/SingleInstance` coordinates over a D-Bus name claimed before the pipeline is built — a second launch has to reach the running player and exit without ever opening the audio device, because two processes briefly holding the same sink is audible and this happens on every file opened from a file manager.

Verified against a running player, one process throughout: a bare second launch enqueued and left a paused track paused (`1 of 2` → `1 of 3`); `--enqueue` appended without disturbing what was playing (`1 of 4`); `--play` started the file it was given; `--replace` cleared the list and played (`1 of 1`). Each secondary exited with status 0.

**The first and third acceptance clauses only look contradictory.** One says a second launch enqueues, the other says paths fill the playlist and select the first without playing. They are the same rule seen from two starting points: appending to an empty list *is* filling it, and selecting the first arrival is right only when there was no cursor to disturb. So the bare default appends in both cases and selects only when the playlist was empty; `SingleInstance` applies that downgrade, being the part that knows whether anyone else is running.

**`--replace` starts playing, and that is a judgement rather than a reading.** F-052 does not say. Replacing the playlist removes whatever was playing from it, so the alternative is to leave the user in silence after an explicit command — a worse surprise than starting the list they just asked for. BUG-015's rule is not violated: it governs the *bare* default, and the whole point of that entry is that an explicit form may do what the implicit one must not.

**With no session bus, every launch is its own player.** That is the same way F-050 and F-051 degrade, and it was the behaviour before any of this existed. Confirmed: two launches without a bus produce two players.

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
