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

- Documentation brought back into line with the tree after Phases 4 and 5.
  ARCHITECTURE.md described a `qml/panel/` and `qml/meters/` that never existed,
  display modes swapped by a `Loader` over a context property when they are
  `ShaderEffect`s shown one at a time, a texture layout of one channel per
  quantity when red and green together carry a 16-bit magnitude, and a
  `platform/` directory that is still Phase 6. It gains `ui/` and the token
  system, which it had never mentioned.
- CLAUDE.md's state section still said Phase 4 was in progress with `resources/`,
  `tools/` and every shader absent. It is the document a fresh session reads as
  truth, so it now carries the current inventory, the six suites, and the half
  dozen things that were established the hard way and would cost a day each to
  rediscover.
- BUILD.md gains what the measurement tools need — `wmctrl`, `x11-utils`,
  ImageMagick, `python3-pil` — none of which is needed to build or run the
  player, and a warning that `ctest` without the two audio-file paths runs four
  suites of six and reports a pass.
- IMP-004 and IMP-005 were both deferred against triggers that have since
  fired: a fifth test suite arrived twice over, and Phase 6 is next. Recorded
  rather than acted on, but recorded, because a deferred entry whose condition
  has passed silently becomes a forgotten one.

- The README's screenshots retaken, and the single theme image replaced with a
  four-up of every finish. Each panel in it has the chooser open with its own
  finish latched, which shows both what the finishes look like and that they are
  selected rather than built.

- F-036, adjustable display proportions: settings move out of the panel and into
  a window of their own, and gain the nine display *proportions* the meter shaders previously carried as literals:
  the bar gap and cap thickness, the flame's ranks, front and back height,
  parallax and softness, and the ladder's segment count and over-reference. Each
  was chosen by eye and is a preference rather than a fact, so it belongs to the
  user. `ui/VisualSettings` clamps every one on the way in — from the sliders
  and from a hand-edited settings file alike — because a flame with zero ranks
  or a ladder with zero segments is a division by zero in a shader, which does
  not throw but draws something wrong sixty times a second. Eleven keys added to
  SPEC.md §Settings; nine new checks in `meters_test`. The explanatory note
  under those keys had been left *between two rows* of the settings table, which
  ends the table and leaves everything after it without a header; it now sits
  below the table. A second window rather
  than a drawer because a drawer pushed the playlist down and covered the very
  display being adjusted, and these are settings that can only be judged by
  watching the meter move while they change.

- Phase 6 opens with `app/Player` (IMP-005). The engine, the playlist, the
  metadata reader and the meter source, and all fifteen arrows between them,
  move out of `main()` — which drops from 378 lines to 307 and no longer holds a
  single `connect`. `app/` is a new module and is declared to be the only one
  allowed to depend on several peers: `core/`, `library/`, `meters/` and `ui/`
  include nothing from one another, and each is testable alone because of it, so
  putting the facade in `core/` would have turned "core owns the pipeline" into
  "core owns everything". `platform/` will call into `Player` rather than into
  `core/`, so the desktop code couples one way. The facade adds no policy: play
  order still belongs to the playlist and the three-second rule on `previous`
  still belongs to the engine. Verified by driving the panel under XTest, since
  no suite covers the wiring and the wiring is all that moved.

- Settings persistence becomes `platform/Settings`, which is what ARCHITECTURE.md
  had promised since before that directory existed. Every key in SPEC.md
  §Settings now lives in one class, restored onto the objects and written back on
  `aboutToQuit` through a single connection instead of three. `main.cpp` falls to
  205 lines from the 378 it began the phase with and holds neither a `connect`
  nor a settings key. The window is taken as a plain `QObject` and read by
  property name, so `platform/` needs no Qt Quick dependency to remember whether
  the panel was folded. Verified by round trip: an existing settings file
  survives a launch and a close byte for byte, which is the property BUG-019
  broke — a key written on exit and never read on start comes back as its
  default, and a diff shows it.
- IMP-007 logged, not applied: SPEC.md's settings table gives no way to tell its
  twenty-four implemented keys from its three unimplemented ones.

- MPRIS2 (F-050): `platform/MprisService` exports `org.mpris.MediaPlayer2` and
  `org.mpris.MediaPlayer2.Player`, which is what makes the media keys work, puts
  a track title on a lock screen and gives a panel applet its transport buttons.
  It talks only to `app::Player`, so a lock screen and a button on the chassis go
  down the same path and cannot diverge — which is the reason the facade was
  built first. Every property, method and signal was exercised with a bus client,
  and Cinnamon was observed calling `GetAll` on both interfaces unprompted.

  Two things about MPRIS are easy to ship broken and are handled explicitly.
  **Qt does not emit `PropertiesChanged` for an adaptor's properties**, so a
  service can be correct to a client that polls and stale to one that subscribes;
  every signal is built and sent by hand. And **`Metadata` is coalesced to one
  emission per turn of the event loop**: a track change arrives as three separate
  signals, and publishing on the first sent a map holding the new title against
  the previous track's URL. It corrected itself a moment later, which is what
  would have made it the kind of defect nobody reports.

  `Engine` gains a `seeked` signal, because `positionChanged` fires every frame
  and says nothing about how the position got there. MPRIS's `Seeked` needs the
  discontinuity and not the progress; without it, a seek made on the panel would
  leave every lock screen showing a position that never happened.

  `Qt6::DBus` is linked by the application target alone — no test suite acquires
  a dependency on a session bus. A machine without one warns and plays on, since
  losing the desktop's media controls is not a reason to refuse to play music.

- Media keys (F-051): `platform/MediaKeys` registers with the desktop's settings
  daemon, and the feature note it was written against turned out to be wrong in
  the expensive direction. It said the keys would be "delegated to MPRIS rather
  than a global grab" under Wayland, implying a grab under X11 — but **the major
  desktops do not route media keys through MPRIS on either display server**.
  GNOME, Cinnamon and MATE own them in a daemon and hand them to whichever
  application last registered over `org.gnome.SettingsDaemon.MediaKeys`, an
  interface that predates MPRIS and has outlived several attempts to retire it.
  Measured rather than reasoned: with F-050 running and correct, a synthesised
  `XF86AudioPlay` left the player exactly where it was. The split is by desktop,
  not by display server.

  Verified with real `XF86Audio` keysyms against a running player: play toggles,
  next and previous move through the list, stop stops. Registration is watched
  and renewed, because the grab belongs to the D-Bus connection and lapses when
  the daemon restarts — a player that registers once at startup and then quietly
  stops answering the keys is the failure that avoids.

- IMP-009 applied, from the author noticing that the F-051 testing had paused a
  browser video: the media keys are claimed on activity rather than on
  existence. They are a single global thing only one application can hold, and
  the daemon gives them to whoever registered last — so registering at startup
  and holding until exit takes them from a browser that is actually playing.
  Ferrolux now holds them while its window has focus, and while it is a session
  that has played and has something to resume; a pause keeps them, a stop hands
  them back.

  Reading the engine's state was not enough, and put the defect straight back:
  a track loaded from the command line sits at `Paused` on row 0 having never
  made a sound, which is the same state as a session paused halfway through, so
  "playing, or paused with a track" claimed the keys at launch. What is
  remembered instead is whether playback has happened since the last stop.
  Verified by watching the grab and release calls on the bus rather than by
  pressing keys, so the test could not disturb whatever else was playing —
  which is how the original defect was found.

- IMP-008 applied: `platform/X11MediaKeys` grabs the four `XF86Audio` keysyms
  for sessions with no settings daemon — X11 under a bare window manager, the
  case F-051 could not reach. The entry had said to wait for a session to test
  in; a nested X server with no window manager and no daemon **is** that
  session, and keys synthesised on it cannot reach the real desktop.

  Testing it found two defects that reading would not have. `MediaKeys::attach`
  returned early when there was no session bus at all, so the one case the
  fallback exists for could never reach it. And a grab claimed and released by
  activity, inheriting IMP-009's rule, could never be taken: under a bare window
  manager nothing focuses the window and nothing plays until a key starts it.
  **Letting go of a registration gives the keys back to another player; letting
  go of a grab gives them to nobody**, so the grab is held for the run.

  Every combination of the three locking modifiers is grabbed separately, or the
  key works until somebody presses Num Lock; a refused grab is caught rather
  than fatal, and left to whoever holds it. `find_package(X11)` is optional, so
  a Wayland-only or headless build compiles the class to a stub — verified by
  building with X11 disabled and checking the grab is absent from the binary.

- Single instance and the command line (F-052): `platform/CommandLine` parses
  `--enqueue`, `--play` and `--replace`, and `platform/SingleInstance`
  coordinates over a D-Bus name claimed **before the pipeline is built**. A
  second launch has to reach the running player and exit without ever opening
  the audio device: two processes briefly holding the same sink is audible, and
  it would happen on every file opened from a file manager.

  The two acceptance clauses that read as contradictory — "a second launch
  enqueues" and "paths fill the playlist and select the first" — are the same
  rule from two starting points. Appending to an empty list is filling it, and
  selecting the first arrival is right only when there is no cursor to disturb.
  The bare default therefore appends in both cases and selects only into an
  empty playlist.

  `--replace` starts playing, which F-052 does not say and is recorded in the
  feature entry as a judgement: replacing the playlist removes whatever was
  playing from it, so the alternative leaves the user in silence after an
  explicit command. BUG-015's rule is untouched — it governs the bare default,
  and the point of that entry is that an explicit form may do what the implicit
  one must not.

  With no session bus, every launch is its own player, the same way F-050 and
  F-051 degrade. Verified both ways: one process across four launch forms with a
  bus, two independent players without one.

- IMP-010 applied, split the way the entry said it would have to be.
  `tests/platform_test` covers the part that needs nothing but the build —
  `CommandLine`'s wire protocol between two launches, where a drift makes a
  `--replace` from a file manager quietly append instead. It does not drag
  GStreamer in after all: `core/Engine.h` forward-declares its pipeline, so the
  enum costs only Qt. `parse` now takes its arguments rather than reaching for
  `QCoreApplication::arguments()`, which is what makes the flag mapping testable
  at all.

  `tools/verify-desktop.sh` is the rest: MPRIS, the single-instance hand-off and
  each flag form, a session saved and restored, and the settings surviving a
  launch byte for byte. **It is hermetic** — `XDG_CONFIG_HOME` and
  `XDG_DATA_HOME` point into a temporary directory, so it cannot disturb the
  settings of whoever runs it, which every earlier round of this checking did by
  hand. And it asks the player to quit over MPRIS rather than killing it: the
  first draft used `pkill` and six checks failed, because SIGTERM does not reach
  `aboutToQuit` and every save happens there — the tool reproducing a lesson
  already written down, which is the argument for encoding these rather than
  remembering them.

  Eight suites, 355 checks.

- Session restore (F-015): `platform/Session`. The playlist contents go to a
  playlist file under `~/.local/share/ferrolux/`, because that is what a
  playlist file is for and it stays readable by anything else; the track, the
  position and the play order go to SPEC.md §Settings, which gains
  `session/track` and `session/order` and now implements the two `session/`
  keys it had declared and never used.

  **Play order means the permutation, not the shuffle flag.** F-012's acceptance
  is explicit that shuffle is an order the model holds and does not recompute on
  each advance — so restoring `playback/shuffle` and letting the model reshuffle
  would perform exactly that recomputation at launch. Shuffle would be on, the
  list would be in *a* random order, every visible symptom would be right, and
  the tracks coming up would not be the ones that were coming up when the user
  quit. The permutation is saved whole and adopted whole; `session/order` is
  omitted when it is the identity, so nobody who plays in order pays for it.
  Verified across a quit and a restart, byte for byte identical.

  Nothing resumes playing: the position is restored so Play continues where it
  left off, and a player that starts on its own because it was playing when it
  closed makes noise in a quiet room. Three awkward cases were driven rather
  than reasoned about — command-line paths add to the restored session and
  suppress the seek, an order that does not describe its playlist is refused
  with the contents kept, and clearing the playlist removes the keys and the
  file.

  `PlaylistModel` gains `playOrder()` and `adoptOrder()`, the latter refusing
  anything that is not a permutation of exactly the rows present: an order
  indexing entries that are not there is a crash rather than a wrong order.

- Keyboard control (F-043): `qml/Shortcuts.qml` holds every binding and
  `qml/KeyGuide.qml` prints them, **from the same table**. That shape is the
  design rather than a convenience — a reference kept separately from the
  bindings it describes is one edit away from lying, and it is believed for
  months afterwards.

  The substantial part was not the shortcuts but making `Slot` keyboard-operable,
  which is what puts the equaliser bands, the volume, the balance and the seek
  bar within reach: a fader is not reachable from the keyboard if the only way
  to set it is to aim at a lever a few pixels wide. Arrows move by `step`, Page
  Up and Down by `pageStep`, Home and End to the ends, and every one leaves
  through `moved` and `released` exactly as a drag does, so the caller's policy —
  seek on release for the position bar per BUG-009, set at once for a volume —
  applies to a keypress without the control knowing which is which. Focus is
  drawn rather than borrowed, like everything else on the chassis.

  Bare letters are deliberately unused: a `Shortcut` at window scope fires while
  a text field has focus, so a bare `S` for stop would be a letter that could not
  be typed into a preset name. The set is disabled whenever the focused item
  takes typed characters as well, so the sequence choice is a second line rather
  than the only one.

  Verified against a running panel with synthesised keys, including that a
  third top-level window does not bring back BUG-021: the application exits
  cleanly with the guide open, closed by the manager, and never opened.

- IMP-011 applied: the window is `Ferrolux RS-1` rather than
  `Ferrolux RS-1 — Phase 5 harness`, which was accurate when it was written and
  had been what every task switcher and screenshot said since. MPRIS already
  reported `Ferrolux RS-1`, so the desktop had two names for one application and
  now has one. The track is deliberately not in the title: it reaches the shell
  through MPRIS metadata already, and a window that renames itself every three
  minutes cannot be found twice in a task switcher.

- IMP-004 applied, its trigger having fired twice: `tests/Check.h` replaces the
  `check()` helper each of the six suites had its own copy of. The saving in
  lines is roughly nothing — 78 removed, 30 added, and the header is 65 — and
  that was never the point. **The copies had already drifted**: `tokens_test`'s
  `check` was missing the `std::fflush` the other five had, so a suite that
  aborted mid-run lost its buffered tail, which is the part naming the check it
  died on. Several of these drive a real pipeline and can abort. What was given
  up is what the entry's trade-off named — a suite is no longer one file
  readable start to finish without following an include. Output is
  byte-identical and a deliberate failure still reports and exits non-zero.

- IMP-007 applied: `tests/spec_test` holds SPEC.md §Settings and the code to
  each other, **in both directions**. Every key the table states must be one the
  application reads or writes, and every key marked `**Planned.**` must not be —
  a one-way check rots in whichever direction nobody is watching, which is how
  the entry came about: the orphan count fell from three to one when F-015
  landed and nothing anywhere said so. `ui/geometry` is the one that remained,
  and is marked.

  It would have caught BUG-019, where a key sat in the table, in the saving code
  and absent from the restoring code until a lit field on the panel gave it
  away. The coupling to the document is real and not argued away: reflowing the
  table breaks the suite, so the check verifies it found the table at all and
  names the header row it expected. All four failure modes were provoked against
  a copy of the tree before the suite was believed.

  Seven suites now, 328 checks — IMP-004 landed first and made the seventh
  cheap.

- Phase 6's last deliverable: a desktop entry, a scalable icon and MIME
  associations, installed to the standard locations. The icon is drawn in
  `ferric`'s own palette rather than in colours chosen to look similar, and flat
  — no gradients — because an icon is rendered by whatever the desktop ships and
  renderers disagree about gradients in a way they do not about a filled
  rectangle. `MprisRoot` gains the `DesktopEntry` property that was deliberately
  withheld until the file existed, and `setDesktopFileName` names it for
  Wayland, which finds a window's icon that way and not from the window.

  `spec_test` gains a check that the entry's media types are exactly the ones
  MPRIS advertises — the same claim stated twice, with nothing but this making
  the two agree — and that the three places naming the desktop file say the same
  word. `CommandLine` now accepts a `file:` URL as well as a path, because `%F`
  is specified to pass local paths and not every launcher honours that.

- BUG-027 mostly fixed and left open at low severity: `Engine::setNextSource`
  declines to hand over a next source that cannot be opened, so a fault in a file
  nobody has reached yet no longer ends the track being listened to. A 6.966 s
  file whose next entry is missing now plays 6.965 s of it, against about 5.3 s
  and a stop before. The check is one `stat` on the main thread, never in
  `aboutToFinish`, which is the one function in this project that runs on a
  streaming thread. What remains is a next entry that exists, is readable and
  does not decode: it still costs the previous track its last two seconds, and a
  `stat` cannot tell that a file will not decode.

  The investigation stands in the entry: a broken *next*
  playlist entry stops the track that is playing, because `playbin3` reports a
  failure to prepare the next URI on the same bus as a failure of the current
  one. An error *can* be attributed to its URI, by walking up from
  `GST_MESSAGE_SRC` to the nearest object with a `uri` property, and ignoring
  next-URI errors keeps the current track playing its full length — but no EOS
  ever follows, so the playlist never advances. Ignoring alone would trade "cut
  short and stopped" for "complete and stuck".

- F-002 is Complete. Next and previous had gone unverified for four phases
  because the transport harness could not reach them: play order belongs to the
  playlist (invariant 5), so neither can be exercised against `Engine` alone, and
  the status still said "next has no meaning until F-012" — which stopped being
  true when F-012 landed in Phase 2. `app/Player` owns both, so the harness now
  links it and checks that next advances *and plays*, that previous past three
  seconds restarts the track and stays put, that inside the window it steps back,
  and that at the first entry it neither wraps nor stops.

  The 100 ms clause had never been measured at all. It is now: all five commands
  return in 0 ms. What is timed is the call rather than the time to audible
  output, which is a different promise — the point is that a transport must not
  block, and `gst_element_set_state` can be made to. 375 checks.

- F-031 is Complete, and closing it needed both of its unmet clauses opened up
  rather than one. The 60 fps measurement now exists: on BUILD.md's reference
  hardware at 3840×2160 the spectrum runs at **6.721 ms mean, 59.7% headroom**
  and the mirrored variant at **6.452 ms, 61.3%**, offscreen with a fence after
  each frame and zero late frames. Every mode passes the same sweep, the worst
  now being the VU at 9.897 ms and 40.6% — against AV-002's requirement of 30%.

  The first run of that measurement said 4.807 ms and 71.2%, and closing the
  clause on it would have been closing it on nothing: the benchmark was drawing
  with undefined shader parameters, which is BUG-028 below.

  The second clause asks for peak-hold caps with *configurable* decay, and the
  rate had been a compile-time constant since Phase 4 — a requirement that read
  as satisfied because the visible half of it was. `meters/peak-fall` is now a
  setting, on the panel beside the meter's other two, clamped to 2–60 dB/s:
  at zero a cap does not fall slowly, it sticks, and the display gives a viewer
  no way to tell that from a signal still present. It belongs to `MeterSource`
  and not to `VisualSettings`, because it is a property of how the meter reads
  rather than of how the bars are drawn; the 1500 ms *hold* stays fixed, being a
  fact about how long a person needs to catch a peak rather than a matter of
  taste. `meters_test` checks the resulting rate against the number requested,
  not merely that the cap comes down. 379 checks.

- AV-001 has implemented detection, which leaves AV-007 as the only Critical
  vector without it. The vector is application work on a GStreamer streaming
  thread — a failure whose symptom is an occasional dropout on somebody else's
  machine under a load this one never sees, which is what makes it Critical and
  what makes it expensive to find later.

  Two halves, as the entry specified. `spec_test` reads `src/` and collects
  every GStreamer signal the project connects, holding that set against an
  inventory that names the thread each runs on — so a new callback fails the
  suite until somebody classifies it, the hazard being arrival on the audio path
  without noticing rather than bad code written there deliberately. It then
  brace-matches the body of `about-to-finish`, the only callback that does run on
  a streaming thread, and checks it is free of seventeen constructs: signal
  emission, logging, allocation, blocking cross-thread calls, I/O, pipeline
  state changes and queries. It also asserts that no `gst_bus_set_sync_handler`
  exists anywhere, that being the likeliest way this invariant would break,
  because it is what the documentation reaches for when a bus message needs to
  be seen sooner. Both checks were made to fail before being trusted.

  `tools/stress-audio.sh` is the half that runs: twenty three-second tracks —
  short, so the once-per-track callback is actually exercised — played first
  idle and then under one spinning thread per core with continuous disc I/O,
  timing the callback from inside it with the new `core/StreamTimer` and
  counting `pulsesink`'s own underflow reports. The underrun count comes from
  the sink's logging rather than from a probe on purpose: a probe installed to
  detect starvation would be one more piece of application work on the thread
  under suspicion.

  **Its first run reported the application at fault, and it was not.** Timed as
  one number the callback read 2.4 ms with eight of fifteen calls over budget,
  on an idle machine. Split into the application's own work and the handover it
  performs, the answer inverts: our part is 4.6 µs at worst under full load
  against a 1 ms budget and does not move with load, and the milliseconds belong
  to `g_object_set(playbin, "uri", ...)` — not application work but the entire
  mechanism `about-to-finish` exists to be answered by. The sink never ran dry in
  either condition, at either figure.

  That handover cost is logged as IMP-012 rather than left in the measurement,
  because it doubles under load, from 2.0 ms to 4.2 ms, and nothing in this
  project bounds it: not the margin, which is the sink's buffer, and not the
  cost, which is `playbin3`'s. It is sufficient on the evidence and on this
  hardware. The entry says what would have to change if it ever stops being.
  385 checks.

- AV-007 has implemented detection, which was the last Critical vector without
  one — Phase 7's third deliverable is met. The vector is a texture uploaded
  from the wrong thread: legal under the basic render loop, where the render
  thread and the GUI thread are the same thread, and undefined under the
  threaded one, on somebody else's driver.

  `spec_test` guards the line that decides it. The connection to
  `beforeSynchronizing` must be `Qt::DirectConnection` so the slot runs on the
  thread that emitted it, nothing in the file may be queued onto the GUI thread,
  and the two halves of the split are brace-matched and held apart: the staging
  path may touch no scene graph resource, and the synchronising path must be the
  only place in the file a texture is created.

  `tools/verify-render-thread.sh` is the half that runs. The new
  `meters/RenderThreadGuard` records the thread each `MeterTexture` was built on
  — the GUI thread, since QML instantiates items there — and counts every upload
  and staging call against it. The tool plays a tone so there is something to
  upload, forces `QSG_RENDER_LOOP=threaded`, and runs under `opengl` and then
  `vulkan` with the validation layer requested. Both backends: **~700 uploads in
  twelve seconds, none of them on the GUI thread, nothing from the validation
  layer.**

  **It refuses to pass a run that could not have failed.** Each backend is first
  checked for having seen two distinct threads, and then for having uploaded
  anything at all. Without those, a run under the basic loop and a run in
  silence both report a flawless green, and the first of those is precisely the
  false negative the vector is about.

  Confirmed against the real defect, which behaved exactly as the entry written
  five phases ago predicted. Making the connection queued — one word — put **489
  of 489 uploads on the GUI thread under OpenGL, where the application carried on
  running normally**, and **segfaulted within a second under Vulkan**. A
  developer working under OpenGL and the basic loop would have shipped it.

  The tool needed two corrections of its own before it could be believed. Its
  first version counted Qt's line announcing that the validation layer was
  *enabled*, so requesting validation was itself the finding, on both backends;
  and it reported the segfault as a backend being unavailable, when a crash is
  the vector happening rather than the vector going unmeasured. Both are
  recorded, because that is now the fourth measurement tool in this project to
  have first reported its own artefact.

- AV-011 and AV-012 were found to be describing themselves wrongly. Both read
  `not implemented` while `meters_test` had been checking exactly what each
  asked for since Phase 4 — a tone at each display band's own bin peaking in
  that band and no other, 20 of 20; and 99% needle deflection at 302.0 ms inside
  IEC 60268-17's ±5%, overshooting 1.16% against a specified 1–1.5%. The
  coverage was never missing, only the status, which is the same failure the
  file is about one level up. 392 checks.

- F-004 is Complete. Its taper and its pan law were verified in Phase 1; the
  third clause, that both persist across restart, had been checked by hand once
  and by nothing since. Nothing could check it: the acceptance harness runs
  headless and exits without `aboutToQuit`, which is where settings are written,
  so it can only observe a file nobody saved. `tools/verify-desktop.sh` quits
  over MPRIS and therefore can.

  Six checks, seeded with a volume of 0.42 and a balance of −0.35 against
  defaults of 0.7 and 0, so that a player ignoring the file cannot produce a
  passing result. The restored volume is read directly off the engine through
  MPRIS, changed while running, and found in the file after a clean quit and in
  the next launch.

  Balance has no D-Bus surface and cannot be observed in a running player, and
  does not need to be. `Settings::save()` writes what the engine holds rather
  than copying the file forward, so a distinctive balance that comes back out of
  the file has been through the engine twice — restored on start, read back on
  exit. Both halves were confirmed by breaking them: ignoring the stored volume
  fails two checks and names the default it substituted, and ignoring the stored
  balance alone fails one with a file that has come back as 0.

  Three Must-priority features remain: F-020, F-032 and F-033.

  Adding the coverage turned up BUG-029 in the section beside it, fixed below.

- F-020 is Complete. Its third clause — that adjusting a band takes effect
  "without an audible click" — had been asserted since Phase 3 on the strength
  of a check that the ramp *interpolates*, which is a statement about a number
  in the process rather than about the signal coming out of it. It is now
  measured: a 1 kHz tone captured in real time through the real elements while
  band 4 is taken to +12 dB, once through the ramp and once written straight to
  the element, with the high-frequency content of each 5 ms block measured
  against the tone it rides on. Ramped, the worst 5 ms sits **103.2 dB below the
  tone** against a capture floor of −117.2 dB. Sixty decibels down is already
  inaudible in a quiet room.

  **The measurement overturned the reason the ramp was written.** The unramped
  change is inaudible too, at −98.2 dB: a 5 dB difference, not the difference
  between a click and none. A gain change in `equalizer-nbands` moves biquad
  coefficients while the filter state carries across, and the element re-reads
  its gains once per buffer regardless — BUG-006 — so what would be a click in a
  naive gain stage is already a smooth transition. The ramp stays, because it
  measurably improves on that, because it gives the panel an applied-gain
  readout, and because it would be what stood between this equaliser and a click
  if the element were replaced. But it is insurance, and the status implied it
  was the mechanism.

  Three metrics were needed, and the two wrong ones are kept in the source
  because each produced a plausible number. The largest step between adjacent
  samples found no discontinuity even unramped — there is none to find, for the
  reason above. Removing the best-fitting sinusoid and measuring the residual
  found a large one for the ramped case too, because the fit assumes a constant
  amplitude and a gain change is an amplitude change: it was measuring the
  feature working. Only high-frequency content separates them, that being what
  makes a click audible in the first place. Two Must-priority features remain,
  F-032 and F-033. 397 checks.

- F-033 is Complete, leaving F-032 as the only Must-priority feature still open
  — and its outstanding clause is a judgement about whether the VU needle reads
  like a real deck, which no tool can make.

  The clause here was that switching display modes must not interrupt audio or
  drop a frame. `measure-frames.sh` never could answer it: it sweeps the modes
  one at a time and measures each in steady state, settling the GPU between
  them, which is the right way to ask what a mode costs and the wrong way to ask
  what changing one costs. The event lives in the gap that tool leaves between
  its measurements.

  `tools/verify-mode-switch.sh` measures with the swap interval left **on**,
  which is the reverse of what AV-002 does and deliberate: a dropped frame only
  exists where something is pacing them. With vsync off the loop free-runs and a
  missed frame is indistinguishable from a fast one. Switching goes through
  `MeterSource::cycleMode`, the same call the panel makes when the display is
  clicked, so the tool cannot pass by way of a path the user does not take.

  **Counting the run's dropped frames would not have answered it, and the first
  run proved that.** The steady case dropped two frames and the switching case
  one — this machine drops one or two in twenty seconds while idle, sharing a
  GPU with a compositor — which says only that both are noisy. Relaxing the
  threshold until that passed would have been fitting the test to the answer. So
  each switch now opens a 100 ms window and the drops inside those windows are
  counted apart from the rest: a frame lost to a mode change lands within a
  frame or two of it, and one lost to the compositor lands anywhere.

  Measured over 20 seconds at 60 Hz with audio playing: **51 switches, no
  dropped frame in any window, no sink underruns**, worst interval 22.9 ms
  switching against 23.9 ms steady. Confirmed against the defect by stalling
  60 ms on each mode change, which the check caught as 37 of 38 switches
  followed by a dropped frame while the steady control stayed clean.

- F-032's three own acceptance clauses are confirmed met, and the clause that
  has been carried in its status is Phase 4's rather than one of its own: that
  the needle be visually indistinguishable from a reference deck fed the same
  material. Part of that turns out not to be a judgement.

  `meters_test` now holds the needle to the voltage law at eight marks from
  −20 dB to +3 dB rather than at the single −6 dB point it checked before. That
  is the law a VU face is marked against: because deflection is linear in
  amplitude, −20 dB belongs at a tenth of the sweep and −3 dB at seven tenths.
  The instrument reads where a deck's would across its whole range.

  The face does not. `vu.frag` draws its ticks as `fract(sweep * 8.0)` — eight
  marks evenly spaced in amplitude — and there are no numerals anywhere, while
  SPEC.md §Meters says the scale crowds towards its left end as a real one does.
  Nothing crowds. Logged as BUG-030 rather than fixed, because which marks a
  face carries and whether they are numbered is a design decision and not a
  correction: it is the most recognisable thing about a VU meter, and a viewer
  identifies the instrument by the bunched marks at its left before reading any
  number on it. F-032 stays Partial, now blocked on something specific.
  398 checks.

- D-010's deliverable is ticked, and what was outstanding turned out to be the
  checking rather than the decision. The licence was settled on 2026-09-02 —
  GPL-3.0-or-later — `LICENSE` has carried the unmodified GPLv3 since, both
  READMEs state it, and all eighty-six source files already had their SPDX
  headers. Nothing held any of that true.

  `spec_test` now does: that `LICENSE` is the GPLv3 and contains the patent
  grant version 3 was chosen for, that every `.cpp`, `.h`, `.qml`, `.frag`,
  `.vert` and `.sh` in `src/`, `qml/`, `tests/` and `tools/` opens with an SPDX
  header, and that each one names GPL-3.0-or-later. Build directories are not
  walked, because holding Qt's generated moc sources to a decision about
  Ferrolux would be holding the wrong thing. It also checks D-012's bundled
  fonts ship with their own licence texts — the one licensing mistake this
  project could make by omission rather than by commission.

  Confirmed by adding an unmarked file and an MIT-marked one, each of which
  fails its own check and is named in the failure. A file added without a header
  is a licensing defect that no build, test or review step would otherwise
  report, and this is a release gate. 413 checks.

- `BENCHMARKS.md` exists, which is Phase 7's fourth deliverable. Every measured
  claim the project makes, taken from one tree in one session on the reference
  machine, on a live desktop rather than an idle one because nobody uses an idle
  one.

  **The frame figures are ranges.** The sweep was run three times a minute
  apart: a single run on a shared GPU measures the shader and whatever else
  wanted the GPU that second. Means are stable to about ±3%.

  **Gathering them found two figures that had drifted, both flattering.** The VU
  mode's headroom was recorded on 2026-09-08 as 46.2%, and described as better
  than the 40.6% the mode managed before BUG-030 put marks on its face — a
  comparison between two single runs in different thermal states rather than a
  measurement of the change. Three runs put it at 38.4 – 41.0%, and the claim
  that the marks made it faster is withdrawn. F-031's spectrum figures had the
  same shape of error: 6.721 ms and 59.7% quoted from one run, against
  6.99 – 7.28 ms and 56.3 – 58.1% measured. Neither changes a verdict; every
  mode holds AV-002's 30% floor. Both are why the file quotes ranges, and both
  entries have been corrected rather than left to be believed.

  One of the three sweeps also failed, on a single flame frame at 26.592 ms out
  of six hundred against a 44.4% headroom mean. `measure-frames.sh` marks that
  MISSED while its own closing note says a lone spike is not attributable to the
  shader. Logged as IMP-013 rather than adjusted, because a benchmarks file is
  not where a tool's pass rule should quietly get loosened.

- A `.deb` exists, and half of Phase 7's packaging deliverable is met.

  CPack builds it from the configured tree, taking the version from
  `project(... VERSION ...)` — which is what BUG-032's fix was for: a package
  saying one number while the binary inside it said another is exactly what that
  bug would have produced the first time a package existed.

  **The dependency list is half automatic and half hand-written, and the
  hand-written half is the important one.** `dpkg-shlibdeps` reads the binary and
  finds Qt, GLib, GStreamer core, TagLib and X11. It cannot find `flacparse` or
  `wavpackdec`, because GStreamer opens plugins through its registry at run time
  and nothing in the binary names them — so the four plugin sets are declared by
  hand. A package without them installs cleanly, launches, draws a panel and
  plays nothing, which is F-001's failure delivered by the packaging rather than
  by the code.

  `tools/verify-package.sh` checks it in seventeen checks: that the package
  version is the project version, that it declares the plugin sets, that it
  ships the binary, entry and icon, and that the extracted package **starts,
  plays and quits from outside its build tree**. That last one is the check
  BUG-023 did not have — the application ran perfectly from its build directory
  and showed no window anywhere else, for five phases, because every test ran
  the binary where it was built. Nothing is installed, so the tool can be run on
  the development machine; the cost is that a missing plugin set would not be
  caught here, only by a real install on a machine without it, and the tool says
  so.

- **The Flatpak is a manifest and has never been built.**
  `packaging/org.ferrolux.Ferrolux.yml` is written from the project's known
  requirements — the display, audio and DRI sockets, read-only music, both bus
  names the application claims, and the settings-daemon name F-051 needs. But
  `flatpak-builder` is not installed on this machine and neither is the Qt
  runtime, so nothing in it has been compiled or run.

  Two things in it are known to be unsettled and are marked in the file. The
  TagLib checksum is a deliberate placeholder, left obviously wrong so a build
  fails loudly rather than fetching an unverified tarball. And Flatpak requires
  the desktop entry to be named for the app id while `setDesktopFileName` says
  `ferrolux`, so the window may not find its own icon inside the sandbox; the
  fix is probably to prefer `$FLATPAK_ID`, and it is not done because it cannot
  be tested here and a change made blind to a working code path is worse than a
  known gap.

  It also targets `org.kde.Platform` 6.10, six minor versions ahead of the Qt
  6.4.2 that BUILD.md pins and every figure in BENCHMARKS.md was taken on.
  Several of this project's workarounds are specific to what 6.4 does — BUG-023's
  import path most of all.

  The packaging deliverable therefore stays open, and `v1.0.0` is not tagged.

### Fixed
- **BUG-032: the build and the binary reported different version numbers.**
  `CMakeLists.txt` declared `project(ferrolux VERSION 0.1.0)` while `main.cpp`
  called `setApplicationVersion("0.2.0")`. Both are real — the first is what
  CMake gives a package built from the tree, the second is what `--version`
  prints — and they had disagreed since Phase 2.

  Nothing had gone wrong because nothing has been packaged or tagged, which is
  also what made it easy to leave: the disagreement has no symptom until the
  first `.deb` exists, and then the package says one number, the binary inside
  says another, and the person who notices is a user. D-008 settles which
  versioning scheme to use and says nothing about where the number lives; two
  copies of a number that must agree will not.

  Fixed by keeping one. `project(... VERSION 0.2.0 ...)` is the source, CMake
  passes it as `FERROLUX_VERSION`, and `main.cpp` reports that — proved by
  setting the CMake version to 9.9.9, watching `--version` follow, and setting it
  back. `spec_test` holds the arrangement, including that no literal has crept
  back into the line that reports it. 0.2.0 rather than a new number, because it
  is what the application has been telling anyone who asked; Phase 7's `v1.0.0`
  tag is where it next changes, and it now changes in one place. 425 checks.

- **BUG-027: a corrupt *next* playlist entry truncated the track that was
  playing.** `playbin3` reports a failure to prepare the next URI on the same
  bus as a failure of the one playing, so a fault in a file nobody was listening
  to yet ended the one they were. A next source that cannot be *opened* had
  already been declined, covering a playlist pointing at files that have moved;
  a `stat` cannot tell that a file will not decode, and that was what remained.

  The entry had the diagnosis and two of three pieces since 2026-09-07. The
  third was where to attribute an error from: `playbin`'s `uri` property is
  shared state and has already been changed to the next file by the time the
  error arrives, so asking it is asking the thing that caused the confusion to
  resolve it. `Engine` already knew, because `aboutToFinish` writes
  `m_handoverUri` — so the owning URI is walked out of the message source's
  parents and compared against that, and the walk stops before the pipeline
  itself for the same reason. An error that cannot be placed below it is treated
  as the current stream's, which is the answer that was always given.

  The missing EOS is supplied by `poll()`, which watches for a position that has
  stopped moving within three seconds of a known duration, and only while a
  handover is known to have failed — a still position is what finishing looks
  like, but it is also what a stall looks like, and the duration says which.
  That turns "complete and stuck" into "complete, ended and moved on".

  **The first version of the fix reproduced the bug through its own
  machinery.** It cleared the armed flag on the first error, and a source that
  cannot be prepared does not report once: typefind said "Could not determine
  type of stream" and then "Internal data stream error" from the same element,
  and the second message fell through and ended the track exactly as before. The
  classification now outlives the failure and is cleared when a new source is
  set — the moment that URI stops being the next one and becomes the current one.

  A third change is what actually made it deterministic. Attribution and the
  supplied EOS both act *after* `playbin3` has been told to prepare a file it
  cannot, and `playbin3` shares one `uridecodebin3` between the current stream
  and the next — so tearing it down for the failed one sometimes took the
  current stream's buffered tail with it, at one run in three. `setNextSource`
  now asks `gst_type_find_helper_for_buffer` about the first 16 kB on the main
  thread, alongside the `stat` it already did. Refusing the handover cannot lose
  that race, because the race never starts.

  Measured in `acceptance_transport`, which carries both halves: a track whose
  next entry is readable rubbish plays **32.507 s of 32.507 s** every run,
  against 30.493 s through the same harness before these changes and the 5.035 s
  of 6.966 s first recorded.

  **BUG-027 is narrowed rather than closed.** A next entry whose *type* is
  recognised and whose contents will not decode — a file beginning `fLaC` and
  continuing with anything else — cannot be refused in advance, and the teardown
  still costs the current track **1.464 s of its tail, deterministically**. It
  no longer stops: it plays into its last second and a half, the playlist moves
  on by itself, and the broken entry is stepped over. The test asserts full
  length for the first case and only what is true of the second, printing the
  measured shortfall as a note rather than a check. 421 checks.

- **BUG-030: the VU face's scale marks were evenly spaced, so it did not crowd
  as a real one does.** SPEC.md §Meters says the scale crowds towards its left
  end because deflection is linear in amplitude; `vu.frag` drew its ticks as
  `fract(sweep * 8.0)`, eight marks evenly spaced in amplitude, and nothing
  crowded. It is the single feature by which a viewer recognises the instrument,
  before reading any number on it, and it was what stood between Phase 4's
  reference-deck clause and an answer.

  Fixed to the author's choice: marks at the eleven standard positions, −20
  through +3, and no numerals — the crowding carries the identification, and type
  legible at 3× in a full panel is not legible in compact mode at 1×. The arc's
  travel went from 1.4 to 1.413, which is +3 dB exactly, so the last mark lands
  on the end of the scale rather than just outside it.

  Looking at the result found two older defects on the same six lines. The marks
  had been rendering as faint specks in both the old face and the corrected one,
  because the band giving them their radial length read `(1 - smoothstep(r -
  0.075, r - 0.070, radius)) * step(r - 0.075, radius)` — two factors non-zero
  together only across a sliver a fifteenth of the intended length. And testing
  eleven marks per fragment across the whole sweep cost **2.5 ms of a 16.7 ms
  frame at 3840×2160**, taking the VU mode from 40.6% headroom to 25.3% and
  failing AV-002's floor; `measure-frames.sh` caught that on the first run after
  the change. With the loop confined to the thin band it draws into, the mode
  runs at 8.961 ms and 46.2%, better than before any of this. Every mode now
  holds between 46.2% and 60.1%.

- **BUG-031: three measurement tools wrote a settings file the application does
  not read.** `stress-audio.sh`, `verify-render-thread.sh` and
  `verify-mode-switch.sh` each wrote `ferrolux.conf` to set the volume to zero;
  `QSettings` writes and reads `ferrolux.ini`. Every run of all three played at
  the default 0.7 while the tool's header, and BUILD.md, said it was silent. The
  measurements are unaffected — muting is downstream of the streaming thread,
  the render thread and the frame clock alike — but the room was not.

  Nothing could have reported it: the write succeeded, `XDG_CONFIG_HOME` pointed
  at a temporary directory, and a player that cannot find a settings file starts
  perfectly happily on its defaults. It surfaced only because the same mistake in
  a capture script meant `meters/mode` was ignored too, and a screenshot taken to
  inspect the VU face came back showing the spectrum. `verify-desktop.sh` and
  `verify-scaling.sh` had the name right all along, which is how the wrong one
  went unnoticed beside them.

- **BUG-029: `verify-desktop.sh`'s session check raced a track boundary and
  failed about half the time on correct behaviour.** It read the current track
  and position over MPRIS, quit, restarted, and expected both back — but the
  material is six nine-second tracks and by the time that section ran it read a
  position of 9.12 s, on the boundary every run. Whether the gapless handover
  landed before or after the quit was a coin flip, and the two symptoms were the
  two sides of it: a restored track that did not match the one read, and a
  restored position of zero.

  The player was right in both outcomes. A handover resets the cached position,
  so a session saved just after a boundary faithfully records the new track at
  zero, and restore brings back exactly that — a failing run's log said
  `restored 6 entries at row 5` while its settings file said `track=5,
  position=0`, an accurate account of where playback was rather than of where it
  had been four seconds earlier when the script looked.

  Fixed by placing playback rather than finding it: MPRIS `SetPosition` puts the
  stream two seconds into the current track before the reading is taken, which
  leaves the whole window clear of the boundary. Nine consecutive runs pass.
  Pausing first was rejected — it would make the section deterministic by no
  longer testing what F-015 is about, which is quitting *while playing* — and so
  was comparing the restored state against the saved file, which removes the race
  by putting the file on both sides of the comparison. One check was added to
  assert the placement actually happened, because a `SetPosition` that quietly
  stopped working would otherwise leave everything below passing while measuring
  exactly the race it removes.

  A false failure is worse than no check. This one sat in the tool that verifies
  the session, every settings key and now F-004, and the response a check that
  fails half the time invites is to stop reading it.
- **BUG-028: `frame_bench` measured shaders whose parameters were undefined.**
  The benchmark registered every context property the display needs except
  `Visuals`, so each `Visuals.*` binding raised `ReferenceError` and each
  uniform took its type's default. It ran, reported believable per-frame figures
  and passed — while drawing something the application cannot draw. The flame
  went from 4.292 ms to 7.374 ms once the property was bound: the benchmark had
  been understating the most expensive mode by **72%**, because `flameRanks`
  sets how many layers that shader composites and BUG-016 established it is what
  dominates the cost. This is the tool the 60 fps clauses of F-031, F-032 and
  F-033 are judged by, and it was the second of three measurement tools to be
  wrong in the flattering direction.

  It is a regression rather than a defect the benchmark was born with, and the
  shape of it is the lesson. Phase 4 measured these shaders correctly, because
  the proportions were literals in the QML then. F-036 turned nine of them into
  `Visuals.*` bindings on 2026-09-06 — a change confined to `qml/` and `src/ui/`
  that broke a test which does not compile against either, because the coupling
  is by name at run time. Nothing could have failed to build, and QML's answer
  to an unknown name is a default value. The Phase 4 numbers stand; every
  measurement taken in the day between are the ones that do not.
- **BUG-026: `errorBanner` was called twice and defined nowhere**, so a preset
  that could not be named and an `.eqf` that would not import both raised a
  `ReferenceError` instead of saying so. Fixed with the surface the panel
  already had rather than the banner the calls were named after: `DisplayPanel`
  puts an error where the album would be, on its own principle that "an error
  takes this line rather than getting one of its own", and the panel's own
  notices now share it — the engine's winning where both have something to say.

  Looking at the result found a second defect on the same line. The message
  arrived as "A preset needs a name without a …", because `albumReadout` is
  anchored to `formatReadout.left` and **an invisible item still holds its
  anchor**: the text was being elided to leave room for a field that is hidden
  whenever a message is shown. That one predates the entry and truncated the
  engine's errors too — every message past about thirty characters, on a display
  with the room to show it, unnoticed because the messages seen so far were
  short.
- **BUG-025: a file that would not load stalled the playlist instead of
  advancing**, which was half of F-001's second acceptance clause. Two defects,
  both in the engine. `play()` on a source already in `Error` set the state back
  to `Loading` and asked the pipeline to start, so nothing new was attempted, no
  new error arrived, and the player sat in `Loading` for ever — which MPRIS
  reports as playing with a position that never moves. And nothing turned a
  failure into an advance: `Engine` now emits `sourceFailed` and `Player` steps
  past it, queued so a run of broken files does not recurse, and bounded to one
  full pass so an entirely broken playlist under repeat-all does not circle for
  ever.

  A track that was merely *selected* and turns out to be broken stays selected —
  nothing was playing, so there is nothing to carry on from. The error is held
  on the panel for six seconds after playback moves on: clearing it when the next
  track started was correct and useless, because a skip takes a fraction of a
  second and the message flashed past unread.

  Two of the four cases originally recorded as failures were artefacts of the
  test. `addPaths` sorts, so a file named to sort last had nothing to advance to;
  and a truncated FLAC declares its full duration in its header, so playing
  silence past the cut and advancing at the end is the file being honoured
  rather than a stall. **F-001 is Complete** — both clauses, ten formats.
- **BUG-024: WavPack was advertised in three places and played in none**, fixed
  by supplying the piece that was missing rather than by choosing between the
  two options the entry had framed. Neither was needed. A decoder is only
  reachable if something identifies the stream first, and GStreamer registers
  the two independently — so the format was fully decodable and completely
  unplayable at the same time: `wavpackparse ! wavpackdec` handled every file,
  including one written by the reference encoder and verified lossless by
  `wvunpack`, while the type finder reported `video/x-h264` twice across five
  files and nothing at all three times.

  `core/TypeFinders` registers one that matches the four ASCII bytes `wvpk` at
  offset zero, at `GST_RANK_PRIMARY` rather than higher — if the upstream finder
  is fixed, both match and GStreamer takes the more confident answer, where
  ranking ours above everything would mean out-voting a correct answer with
  ours. It was prototyped before it was recommended: a forty-line programme
  registering the finder took a WavPack file to EOS through `playbin3`, which is
  what turned a guess into an option. **All ten of F-001's formats now play**,
  each checked by loading it into the player and watching the position advance.
- **BUG-023: the application only ran from its build directory**, found by
  installing it for the first time. Copied anywhere else it started, claimed its
  bus name, restored its session, decoded audio — and showed no window,
  registered no MPRIS and spun a core, silently. Qt 6.4's engine does not search
  `qrc:/qt/qml`, where `qt_add_qml_module` embeds this application's own module;
  it searches the directory the executable sits in, and the build tree happens to
  contain a generated `Ferrolux/qmldir`. Five phases of testing never saw it,
  because every test ran the binary where it was built.
- BUG-022: every menu opening logged tens of binding-loop warnings. The width
  binding assigned `metrics.text` and read `metrics.width`, writing to the object
  it depended on; Qt broke the cycle after a bounded number of passes, so the
  width was right and the cost was noise and repeated measurement. Moving the
  measurement into a function — the obvious cure, and the one this was first
  logged with — removes the loop and breaks the menu instead: a `Popup`'s content
  is not laid out until something asks for its width, so the flip's
  `implicitHeight` reads zero and the preset menu unrolls off the bottom of the
  window, hiding four of its nine presets. Fixed by measuring without writing,
  through `FontMetrics.advanceWidth`, keeping the eager evaluation that turns out
  to be load-bearing. The two token reads that establish the binding's
  dependencies look removable and are not; the comment says so.
- BUG-021: once the settings window had been opened, closing the player left the
  process running, windowless and still playing — and opening the player again
  stacked another one behind it. `Qt.quit()` asks every top-level window to
  close and abandons the quit if any one of them refuses, by which point the
  main window has already gone and nothing is left to close. The settings window
  refused, in order to preserve its scroll position. The premise was wrong:
  closing a QML `Window` only hides it and keeps the object and its state alive,
  so the handler bought nothing and cost the shutdown. Reduced to a thirty-line
  program to confirm the mechanism rather than the symptom. Shutdown is now
  verified against four scenarios rather than one — settings never opened, left
  open, toggled shut, and closed by the window manager.
- The `ember` and `glacier` finishes were both wrong, reported from use.
  `ember` read as pink rather than burnt orange, which is what 42% saturation
  at 50% lightness on a warm hue *is* — it is now 60% at 34%, and its ink
  inverts to a light warm grey because the chassis is no longer light enough to
  print on. `glacier` sat at 194° and 10% saturation, a grey with a hint of
  cyan rather than a blue, and light enough to read as pale concrete; it is now
  a dark 215°, within a degree of the lamp's complement, which is what makes an
  amber readout look lit rather than merely present.
- `tools/verify-scaling.sh` pins `ui/theme` to the default set for the duration
  of its run. It finds the edge it measures *by colour*, and those colours
  belong to whichever finish was last selected — so a run inherited from a
  session left on a dark chassis would have found no chassis-to-well boundary
  and reported every ratio as soft. A false failure produced entirely by the
  harness, which is the third this pair of tools has had.
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
