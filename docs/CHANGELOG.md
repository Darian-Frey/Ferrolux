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

- Volume and balance gain readouts, and balance gains a detent at centre with a
  centre mark. The Phase 1 harness had a balance readout; compacting that row
  in Phase 2 dropped it, leaving a continuous control whose most important
  position was invisible. F-040 gains a matching acceptance criterion.
- Native file dialogs: "Add files…" with multi-select, "Add folder…", and real
  choosers for playlist open/save and `.eqf` import. Adds a dependency on
  `qml6-module-qtquick-dialogs`, recorded in BUILD.md.
- `PlaylistModel::addPaths` is now the single route for adding media. It
  expands directories recursively, filters by audio suffix and sorts; files,
  folders, drag-and-drop and command-line arguments all use it. Dropping a
  folder previously added the folder itself as a row.

- `meters/MeterSource`: logarithmic band mapping, asymmetric smoothing,
  peak-hold and second-order VU ballistics, all on the CPU per D-005 and
  testable without rendering. `level` and `spectrum` are in the pipeline, last
  in the chain so the meters show the signal as heard. 22 checks in
  `tests/meters_test`, plus end-to-end acquisition in `acceptance_transport`.
- A plain spectrum and VU readout in the harness — rectangles, not the
  shader-rendered display F-031 and F-032 call for, to prove the data is right
  and correctly timed before any GPU work.
- AV-002 detection: `FrameTimer` measures frame interval and CPU render time
  from the render thread, and `tools/measure-frames.sh` sweeps every display
  mode at three resolutions. All five modes hold 60 fps with zero late frames
  up to 1920x1008, worst interval 16.014 ms against a 16.667 ms budget. The
  4K clause and the 30% headroom clause remain unverified — the window manager
  clamps the window to the display, and the reported headroom covers only the
  CPU pass. AV-002 records both limits and what would lift them.

- AV-002 detection completed with `tests/frame_bench`, a `QQuickRenderControl`
  harness that renders `qml/MeterDisplay.qml` offscreen on the OpenGL RHI with a
  fence after each frame. No window manager to clamp the size and no compositor
  to pace the loop, so it reaches 3840x2160 and its headroom figure is real
  rather than a lower bound. `tools/measure-frames.sh` now runs both passes.
- The meter display moved out of `Main.qml` into `qml/MeterDisplay.qml`, so the
  benchmark measures the shaders the application actually shows rather than a
  copy that would drift away from them.
- `MeterSource.ceiling`: the frame's tallest band, published as a bound the
  flame shader culls empty pixels against.
- Phase 4 acceptance met: all five modes hold 60 fps at 3840x2160 with between
  46% and 64% of the frame budget spare, against a requirement of 30%.

- Phase 5 begins. The design token set is `resources/themes/ferric.json`,
  loaded by `ui/ThemeTokens` and resolved through the `Tokens` QML singleton,
  which is the one place the token names are spelled and the one place the
  panel's scale is applied. Components reference tokens by name and hold no
  literal colours, which is what makes F-044 a token swap.
- The four OFL faces of D-012 are bundled under `resources/fonts/` with their
  licence texts, compiled into the binary, and generated by `tools/make-fonts.sh`
  so what is committed has a stated provenance. `tests/tokens_test` asserts the
  set against SPEC.md value by value and checks each face loads under the family
  the set names — a face that fails to load does not raise anything, it
  substitutes a system font in the middle of a lit readout.
- `qml/Readout.qml` and `qml/DisplayPanel.qml`: the first pieces of the real
  panel. A readout is two text layers at one position — an unlit ghost of the
  all-segments-lit string under the live value — which SPEC.md calls the most
  common tell of a simulated display when it is left out.

- The transport is moulded chrome. `qml/PanelButton.qml` drops its face into a
  well and inverts its lighting from one `depress` quantity, so travel and
  shading cannot disagree; `qml/TransportGlyph.qml` draws the five marks from
  `qml/shaders/transport.frag` as half-plane intersections, antialiased from
  the screen-space derivative and therefore exact at any scale. The legend face
  carries none of those symbols and SPEC.md forbids a fifth face on the control
  surface, so drawing them is the only answer that is not an icon font or a
  bitmap. `qml/Legend.qml` is the printed half of the lit-versus-printed rule.
- The window is the chassis: `shell` behind everything, which is what makes the
  bevel gradients legible as mouldings rather than as rounded rectangles.

- The playlist is a lit well. Rows are in the readout face at three
  brightnesses — one lamp colour rather than three hues — with the playing
  entry marked as well as brightened, a selected row backlit in the dimmest
  amber rather than in the desktop's highlight colour, and a missing or
  unreadable file dimmed rather than reddened: there is no red on this display.
- `qml/SlideSwitch.qml` replaces the cycling buttons for shuffle and repeat,
  which F-040 names as exactly what it excludes. Two detents or three cost the
  same, so repeat is a three-position switch rather than a boolean with a
  special case. The lever reports the state; the marks beneath it are printed
  and never light, because a lit mark would be a second report that could
  disagree with the first.
- `qml/Slot.qml` replaces the position slider, and `qml/PanelSection.qml` draws
  a section as either a well or a raised surface — the same moulding seen from
  opposite sides, which is why it is one component and one boolean.

- The equaliser is panel chrome: a raised surface carrying eleven faders, each
  with a lit gain readout over an unlit ghost and a printed band centre beneath.
  `qml/Slot.qml` now serves both orientations rather than being copied on its
  side, and lights its run from an `origin` — a band at +6 dB lights upward from
  flat and one at −6 dB downward, because what the control reports is a
  departure from flat and not an amount of something.
- Volume and balance are the same control with a printed scale, and balance has
  its detent at centre (F-040). Centre is also its origin, so a centred control
  shows no lit run at all, which is what centred should look like.

- Both toolbars are panel chrome. `qml/PanelMenu.qml` draws the sort and preset
  lists as lit wells — the preset list reports which is in effect by lighting
  it, the sort list has no state to report and so lights nothing —
  `qml/EntryField.qml` gives the filter a lit field with an unlit prompt rather
  than grey placeholder text, and the equaliser bypass is a switch for the same
  reason shuffle and repeat are.
- The transport state and the track counter moved into the display well. They
  are values the instrument reports, and they had been sitting on the chassis
  in the system font since before there was a display to put them in.
- Nothing in the window is drawn from a Qt Quick Controls style any longer.
  What remains of Controls is behaviour rather than appearance: the window, the
  native file choosers, a dialog, a scrollbar, and `Popup` under `PanelMenu` —
  kept because what a popup must get right is dismissal, focus and staying
  inside the window, and a hand-rolled one fails in exactly the ways nobody
  tests.

- AV-005's detection, which had been outstanding since the documents were
  written: `tools/verify-scaling.sh` captures the panel at 1×, 1.5×, 2× and 3×
  and measures the edge rise across a chassis-to-well boundary in device pixels,
  which is the resampling-artefact check as a number. It holds at one pixel at
  every ratio, and each capture reduces back to the 1× panel rather than to a
  different layout. This is the criterion the project exists for, so it is
  measured rather than asserted.

- Compact mode (F-042): the panel folds to a display-and-transport strip and
  back, keeping its position and its playback. The strip is *constrained* to its
  content rather than assigned a height, which is what makes the fold correct
  however it is reached — including from `ui/compact` at startup, a path that an
  assignment inside the toggle function did not cover.
- The equaliser drawer has its own open state, so folding the panel hides it
  without closing it and unfolding returns it to how it was left.

- Volume moved from the mixing section to the transport row, so compact mode
  has it: it is the control reached for most often while listening, and a shade
  that cannot change the volume sends you back to the full panel for the one
  thing you folded it away to stop needing. Moved rather than duplicated — two
  instances bound to one property is the arrangement that drifts.

- The display carries the album and a technical readout — sample rate, channel
  count, codec and bitrate — beside the title. Two thirds of that surface was
  empty, on the largest lit area of the panel.
- `Engine.streamFormat` assembles that line from three sources reporting at
  three different moments: the rate from the spectrum element's caps, the
  channel count from the filter bin's own sink pad, and the codec and bitrate
  from tags on the bus. It prints what is known rather than waiting for a
  complete set that some streams never supply. The channel count is read
  upstream of the capsfilter that pins stereo, or every stream would report
  back the format we imposed and call a mono recording stereo.
- `PlaylistModel` exposes the playing entry's tags, notified both when the
  cursor moves and when metadata arrives for a row that is already playing —
  the second being the one that is easy to miss and the more visible, since
  without it a track starts showing a file name and never stops.

- The transport is a mounted plate. Position, volume and the transport keys sit
  on one raised surface with its name silkscreened beside it, the way a deck
  mounts its transport as a module rather than setting each control into the
  chassis on its own — the same treatment the equaliser drawer already had.
  Compact mode keeps that plate, so the strip is a module rather than a few
  controls left behind.
- Wells have a wall: the shadow the near edge casts across the top of a recess.
  One hairline of lip was doing that job and read as a change of colour rather
  than a change of level.
- A selected row and the current preset are *inverted* — the lamp fills the cell
  and the text is cut out of it in the colour of the unlit display — which is
  how a panel says "this one". A hovered menu option stays merely backlit, so
  the two states cannot be confused. Backlighting alone was too quiet to find.
- Drawn fasteners were tried at the corners of each module and removed: they did
  not read as fittings at panel sizes, and the plate treatment says "mounted
  module" without them. Kept in the history rather than in the tree.
- `MeterDisplay` is a `PanelSection` rather than a bare Rectangle. Its root
  carried `color: "#2C2C2A"` and `radius: 3` as literals predating the token
  set, so a theme could change every surface on the panel except the one the
  meters sit in. Its shaders still hold literal colours, which blocks F-044 in
  the same way; logged as IMP-006 rather than guessed at, because four of them
  are tints the author tuned by eye and no derivation reproduces them.

- Four finishes (F-044): `ferric`, `anodised`, `glacier` and `ember`, chosen
  from a new settings drawer and exchanged while the panel is running. Each set
  is the same file with a different palette, so the geometry and the type scale
  are shared by construction rather than copied and kept in step. The variants
  change the chassis and keep the amber lamp, which is how real equipment
  varies — the paint is the paint and the lamp is the lamp.
- The display can be inverted: lit ground, dark text, the way a filled indicator
  cell works. Persisted under `ui/display-inverted`. The secondary tier is the
  primary ink at reduced opacity rather than a second colour, so it stays a
  fixed distance from the ground whatever the ground is and needs no value
  chosen per set.
- `tests/tokens_test` now holds *every* set that ships to the same vocabulary
  and to contrast floors — BUG-020 turned into a check. A palette is eight
  numbers and it is very easy to write eight plausible numbers that fail; two
  of the three new sets did, by a little, and were tuned until they did not.
- `ThemeTokens` loads by name, lists what ships from the resource directory
  rather than from a table, and falls back with a warning on a name that no
  longer resolves — a set renamed since it was chosen is a stale setting, not a
  reason to start with no appearance.

- The filter field is out of the file toolbar, at the author's request. The
  proxy behind it is untouched — `PlaylistView` is still the model the list is
  bound to and still tested, and the drag-reorder guard that depends on it still
  reads correctly — so what went is the control that set `filterText`, not the
  filtering. F-014 records that its live-filter clause now has no interface
  rather than staying marked Complete: a feature reachable only from a test is
  not a feature the player has. `qml/EntryField.qml` is kept for when that
  clause gets a new home.

### Fixed
- BUG-019: the equaliser preset name was written on exit and never read, so the
  field reported `flat` after a restart over whatever curve had been restored.
  `Equaliser::adoptPreset` takes a remembered name as a label and only when the
  loaded curve still is that preset — resolved the way `applyPreset` resolves it,
  compared to a hundredth of a decibel, and judging a built-in on its bands alone
  because ten bands is all a built-in defines.
- Testing that turned up a second fault of the same kind: a curve that had
  drifted from its preset reported `flat`, the constructor's default, over
  somebody else's bands. `setBands` did not clear the preset name while `setBand`
  always had — moving one band away from `rock` stopped it being rock and
  replacing all ten did not. It marks the curve `custom` now.
- IMP-006 applied: the meter shaders held eleven literal colours, so a finish
  that changed the lamp would have changed the whole panel except the displays.
  Six were substitutions for existing tokens. The other five turned out not to
  need tokens at all — measured in HSL, each is the same lamp at a different hue
  and lightness, which is what a single-phosphor display physically is — so they
  are derived from `readout` in `qml/Tokens.qml`. Fitted to the literals they
  replace and verified twice: worst channel error 3/255 across all five modes,
  and with the lamp temporarily set to VFD green the peak tier tracked it from
  36° to 142° while keeping its 3° offset. SPEC.md's palette stays at eight.
- `qml/Tokens.qml` read its tokens through method calls, which a binding does
  not track, so a theme could never have been exchanged at runtime — the panel
  would have kept whatever it resolved on the first evaluation. It reads the
  maps instead: a property read is tracked, so every component that used a token
  re-evaluates together. The same trap as `mapToItem` in `PanelMenu`, and it
  fails the same way — silently, looking like it works right up until it has to.
- Six literal colours in the meter shaders became tokens, the important one
  being the face the VU needle swings against: without it a dark chassis would
  have arrived with one component still painted for the light one. Four remain
  and are still IMP-006.
- The display showed the file name while the playlist row beneath it showed
  the tags — `01 - Carousel.mp3` under `Blink-182 — Carousel`, the same fact in
  its least useful form. The engine is handed a URL and never sees a tag, so
  the reader had them and the display did not.
- `tests/frame_bench` could not render the meter display after the mode label
  was tokenised: the file it deliberately loads had gained a dependency on the
  `Tokens` singleton, and the bench supplied no tokens. Every run failed with a
  page of "Unable to assign [undefined]". The bench now loads the token set and
  the faces, which is the cost of it measuring the real component rather than a
  copy — and the copy was the worse option.
- The benchmark now settles the GPU between modes. Run back to back at 2160p the
  last of five modes read about 40% slower than the first, which dropped one
  below the headroom floor and looked like a regression until an idle interval
  made it pass. Only one mode is ever displayed at a time.
- The equaliser, preamp, volume and balance figures were hard to read, reported
  from use. They were lit readouts standing on the chassis, and a readout needs
  a ground darker than both of its layers: against the light shell the unlit
  ghost out-contrasted the lit value, so the segments that were off read louder
  than the number that was on. Each now carries its own dark window. SPEC.md
  states `display-bg` as a requirement rather than a label.
- BUG-017: the specified Handjet weight rendered the dot-matrix face as
  continuous strokes, failing the intent stated beside the value that set it.
  `wght` 500 to 300, and `size-readout-large` 13 to 20 — below roughly 20 units
  the elements merge at any weight.
- BUG-018: SPEC.md cited an OFL Reserved Font Name clause that Handjet does not
  invoke. Nothing was broken by it, which is the reason to record it.
- BUG-016: the flame display ran at 26.9 ms per frame at 3840x2160 — 37 fps
  against a 60 fps requirement — with 377 of 600 frames late. Nine receding
  silhouettes at five texture taps each is 45 taps per pixel, and quiet passages
  are the expensive case rather than the cheap one: a tall silhouette lets a low
  pixel stop at the first rank, near silence lets nothing stop anything. Fixed by
  hoisting the loop-invariant derivative, compositing front to back so the loop
  can stop once the pixel is opaque, and culling against a per-frame ceiling
  before any texture is sampled. 26.9 ms to 5.8 ms, output pixel-identical.
- The first version of that benchmark fed a loud signal and reported a pass. It
  now sweeps from silence to full scale, because measuring the best case and
  calling it the average is a worse defect than the one it concealed.
- BUG-012: closing the window left the process running and the audio playing.
  `gst_deinit()` ran while a live pipeline still existed, because the objects
  owning it were locals destroyed only when `main` returned. Now scoped ahead
  of it. A first diagnosis blamed the file choosers for defeating
  `quitOnLastWindowClosed` and was wrong; both are recorded.
- BUG-011: `level` and `spectrum` sit upstream of the sink, so their messages
  arrived a measured 1307 ms ahead of the audio. Frames are now held against
  the running time they carry and released as the clock reaches them.
- BUG-010: SPEC.md specified a first-order VU system with a 1–1.5% overshoot.
  A first-order response is monotonic and cannot overshoot. Corrected to the
  second-order IEC characteristic; measured at 302.0 ms to 99% with 1.16%.
- BUG-013: the VU readout never moved. A QML binding over a plain method call
  tracks nothing, so it evaluated once; and `advance()` never emitted a change
  signal, so even a correct binding would have refreshed at the wrong rate.
- BUG-008: the headroom rule attenuated by exactly the cascade's peak gain, so
  it cancelled every boost. Raising a band lowered everything else, raising the
  preamp did nothing at all, and only cuts worked. Nothing is attenuated now;
  the figure is reported and the manual preamp is the control for level, as in
  every comparable player. AV-003's stated danger — filters driven unstable —
  is addressed by the float chain from BUG-007 instead.
- BUG-009: dragging the position bar snapped back instead of seeking. The
  binding meant to suspend during a drag referred to the property it assigned
  and never suspended, so the per-frame position poll re-asserted the old value
  sixty times a second.
- BUG-007: the audio-filter chain pinned only its channel count, so against a
  16-bit source it negotiated S16LE and the equaliser ran ten cascaded IIR
  biquads in 16-bit integer. Audibly gritty on real music, and present whether
  or not the equaliser was enabled. The capsfilter now pins `F32LE` as well,
  which propagates upstream and puts the whole chain in float.
  `tests/acceptance_transport` asserts the negotiated format against the
  engine's own pipeline, since the equaliser suite's reconstructed chain is
  what missed it.
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
