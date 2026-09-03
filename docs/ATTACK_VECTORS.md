# Attack Vectors

Project-specific failure modes the project must be resilient against.
Grouped by category. Each vector lists detection method and severity.
Severity: Critical (must hold) | Major (regression on release blocks) | Minor (track only).

Ferrolux is pre-implementation, so most detection entries are currently `not implemented`. This is honest signal rather than a gap to be papered over: the distance between an identified failure mode and a working check for it is information. Phase 7 requires every Critical vector to have implemented detection before RS-1 ships.

---

## Audio correctness

### AV-001 Application work on the GStreamer streaming thread
**Severity:** Critical
**Description.** Any allocation, lock acquisition against UI state, blocking I/O or synchronous cross-thread signal on a streaming thread starves the audio path. The symptom is intermittent dropouts under system load rather than a reproducible failure, which makes it expensive to find later and cheap to prevent now. Bus message handlers, metadata callbacks and probe functions are the likely entry points.
**Detection.** Not implemented (would require a thread-annotation pass plus a stress harness running playback under synthetic CPU and I/O load with an underrun counter on the sink). Interim: ARCHITECTURE.md §Key invariants item 1 is checked by review on any change touching `core/` or `meters/`.
**Related decisions.** D-002 (GStreamer backend), D-005 (CPU-side ballistics).
**Related features.** F-001, F-030.

### AV-002 Frame budget overrun from meter rendering
**Severity:** Major
**Description.** The meters redraw every frame at whatever resolution the panel occupies. A shader that is cheap at 1080p may not be at 4K, and the failure mode is a dropped frame rather than an error. Compounding risk: multiple independent position pollers, per-frame QML bindings that allocate, and texture uploads scheduled outside the render pass.
**Detection.** **Implemented**, in two passes that answer different halves of
the question, both driven by `tools/measure-frames.sh`.

*The window pass* runs the real application in a real window at the sizes the
display can render. `src/meters/FrameTimer` hooks `QQuickWindow::beforeRendering`
and `afterRendering` on the render thread and accumulates frame intervals, CPU
render times and late frames into atomics; `FERROLUX_FRAME_MEASURE` runs the
application self-timed, discards the first second so shader compilation and
initial layout do not distort the mean, and prints one line. This is the whole
scene — meters, playlist, chrome, per-frame bindings — and so it is the number
that describes what a user sees. It cannot reach 4K, because the window manager
clamps a managed window to the screen, and its headroom figure is a lower bound,
because the compositor paces frames whatever the swap interval says.

*The offscreen pass* is `tests/frame_bench`, which renders `qml/MeterDisplay.qml`
— the file the application itself instantiates, not a copy — through
`QQuickRenderControl` on the OpenGL RHI. No window, so nothing clamps the size;
no compositor, so nothing paces the loop; and a `glFinish` after each frame, so
the interval is the true cost of producing one rather than the cost of submitting
it. **This is what closes the 3840x2160 clause and the 30% headroom clause.** Its
synthetic material sweeps from silence to full scale, because for the flame mode
quiet is the expensive case and a loud-only signal reports the best case as the
average — see BUG-016.

Measured 2026-09-03. Window pass, five modes at three sizes to 1920x1008: every
mode holds, worst interval 16.03 ms against a 16.667 ms budget, zero late frames.
Offscreen pass at 3840x2160: every mode holds with between 46% and 64% of the
budget spare, zero late frames, worst mode flame at 8.0 ms.

**The detection found the defect it was written for.** On its first run that
could reach 4K, flame took 26.9 ms per frame — 37 fps — with 377 of 600 frames
late, and the level sweep then showed that the quiet passages nobody would think
to test were the expensive ones. See BUG-016; the shader is now 4.6 times faster
with a pixel-identical result.

**One limit remains.** The offscreen pass measures the meter display alone. The
playlist, the chrome and the per-frame bindings that AV-002 names as compounding
risks are in the window pass only, and so are measured only up to 1920x1008.
Their cost does not scale with resolution the way a fragment shader's does, which
is why the split is drawn here, but it is a split and not a proof.

**Related decisions.** D-004 (texture-fed shaders), D-005.
**Related features.** F-031, F-032, F-033.

### AV-003 Clipping and instability from combined equaliser gain
**Severity:** Critical
**Description.** Ten bands at +12 dB with a +12 dB preamp on an already-loud master will clip hard, and clipping in a filter chain can also drive the filters into instability rather than merely distorting. A user who does this has not misused the application; the interface allows it, so the engine must survive it.
**Detection.** **Implemented**, `tests/equaliser_test`. An offline capture feeds
a near-full-scale sawtooth through the real filter chain with all ten bands at
+12 dB and the preamp at +12 dB, and asserts that no sample exceeds full scale
and that none is infinite or NaN. A companion check asserts the pure headroom
arithmetic without a pipeline.

The detection found a real defect on its first run: the original headroom rule
attenuated by preamp plus the largest band gain, which under-attenuated by 9 dB
and clipped the output by 8 dB. See BUG-005.

**The mitigation has since changed shape.** Attenuating by the cascade's true
peak did prevent clipping, and made the equaliser incapable of boosting anything
— the attenuation cancelled the gain that caused it (BUG-008). Nothing is
attenuated now. The vector's stated danger was that clipping "can also drive the
filters into instability rather than merely distorting", and that danger is
addressed instead by BUG-007's fix: the chain runs in `F32LE`, where levels above
unity are ordinary. The detection therefore asserts stability and finiteness
under the worst case, not an absence of clipping, and separately asserts that
the reported excess figure bounds what actually happens.
**Related decisions.** D-006 (equaliser backend), D-007 (band layout and range).
**Related features.** F-020, F-021.

### AV-004 Bus message flood or starvation
**Severity:** Major
**Description.** `level` and `spectrum` both post messages at their configured interval. If the main loop cannot drain them at that rate the queue grows without bound; if the interval drifts relative to the frame rate the meters alias visibly. Either failure looks like a UI problem and originates in the pipeline configuration.
**Detection.** Not implemented (would require a bus queue depth counter exposed in a debug overlay, plus a jitter measurement on message arrival intervals).
**Related decisions.** D-002, D-004.
**Related features.** F-030.

### AV-005 Scaling regression
**Severity:** Critical
**Description.** The founding defect of the project. Any element of the control surface that is authored at a fixed pixel size, or any layout that assumes a device pixel ratio of 1, reintroduces exactly the problem Ferrolux exists to avoid. Most likely entry points are icon assets, hairline borders specified in pixels rather than device-independent units, and shader code that assumes a viewport size.
**Detection.** **Implemented**, `tools/verify-scaling.sh`. The panel is captured at
1×, 1.5×, 2× and 3× device pixel ratio at one logical size, and each capture is
put to two questions that "it looks fine" runs together.

*Crispness* is the **10–90% rise distance across a chassis-to-well boundary**, in
device pixels. This is the artefact check, expressed as a number: resampled
chrome spreads its edges in proportion to the scale, so a bitmap skin would show
this figure growing with the ratio. Vector geometry and a shader antialiased from
the screen-space derivative do not.

*Fidelity* reduces each capture back to the 1× size and compares it with the 1×
capture. It answers a different question — whether it is the same panel or a
different layout that happens to fit — and catches a design that snaps to whole
multiples, reflows, or rounds its metrics onto a pixel grid.

Measured 2026-09-03, a 626×330 logical panel: **1 device pixel of edge rise at
every ratio**, and a mean per-channel difference against 1× of 2.57 at 1.5×, 1.04
at 2× and 1.06 at 3×. The fractional ratio is the worst of the three, which is
what this vector predicts and why 1.5× is in the list at all. Not in CI, because
it needs a display, a window manager and a GPU; it is run by hand and its
captures can be kept with `FERROLUX_SCALING_SHOTS`.

**The measurement's own trap, twice.** A window manager clamps a window to the
*monitor* it is on rather than to the desktop, so asking for a size the largest
ratio cannot fit silently compares a 3× panel with a 1× one and reports the
difference as a defect — the first run read exactly that way. The default size is
now derived from the smallest connected monitor. And a window that opens larger
than the screen is *maximised* by the manager on the way up, after which it
ignores resize requests entirely; the 3× run came back at the full work area
until the script learned to unmaximise first. Both are the same shape of error as
the one recorded against AV-002: a measurement that cannot say what it measured
is worse than none, because it will be believed.
**Related decisions.** D-003 (bespoke vector chrome).
**Related features.** F-040, F-041.

### AV-006 Gapless join failure
**Severity:** Major
**Description.** Gapless depends on `playbin3` receiving the next URI during the `about-to-finish` window. If the playlist model is slow to answer — because metadata extraction is holding a lock, or the next file is on a sleeping drive — the join gains an audible gap. The failure is intermittent and load-dependent.
**Detection.** Not implemented (would require an automated join test: play a known-gapless pair, capture the sink output, assert no discontinuity at the boundary).
**Related decisions.** D-002.
**Related features.** F-005.

---

## Rendering correctness

### AV-007 Texture upload from the wrong thread
**Severity:** Critical
**Description.** Scene graph resources may only be touched during synchronisation and rendering on the render thread. Uploading meter data from a bus handler on the main thread will appear to work under some backends and crash or corrupt under others, which makes it a latent portability failure rather than an immediate one.
**Detection.** Not implemented (would require running the test suite under both the OpenGL and Vulkan RHI backends, and with `QSG_RENDER_LOOP=threaded` forced, since the basic loop hides the bug).
**Related decisions.** D-004, D-005.
**Related features.** F-030.

### AV-011 Low-frequency band collapse in the spectrum display
**Severity:** Major
**Description.** The `spectrum` element produces linearly spaced bands. Mapping those directly onto a logarithmic display without sufficient analysis resolution puts the bottom two octaves — everything below roughly 200 Hz — into a single display bar, so bass content is invisible. This is why SPEC.md specifies 512 analysis bands feeding 24 display bands rather than requesting 24 from the element.
**Detection.** Not implemented (would require a synthetic sweep test asserting that a tone at each display band's centre frequency produces its maximum response in that band and no other).
**Related decisions.** D-004.
**Related features.** F-031.

### AV-012 VU ballistics degenerating into a peak meter
**Severity:** Major
**Description.** A VU meter that responds instantaneously is a peak meter wearing the wrong face, and it reads immediately as fake to anyone who has used real hardware. The risk is not that the filter is written wrongly but that it is quietly bypassed — for instance by a smoothing change made to fix a spectrum problem being applied to both paths, since they share a source.
**Detection.** Not implemented (would require a unit test on the ballistics filter asserting 99% deflection at 300 ms ±5% for a steady reference tone, with overshoot within 1–1.5%).
**Related decisions.** D-005.
**Related features.** F-032.
**History.** Identified during the design session before implementation, because the shared `MeterSource` makes the coupling easy to introduce accidentally.

---

## Data and scale

### AV-008 Large playlist performance collapse
**Severity:** Major
**Description.** Twenty thousand entries is a realistic library-sized playlist. Naive approaches fail in several places at once: synchronous metadata extraction on add, a non-virtualised list view, sorting that rebuilds the model rather than the proxy, and filtering that re-reads tags. The failure is gradual, so it is easy to ship without noticing.
**Detection.** Not implemented (would require a generated 20,000-entry fixture with timing assertions on add, sort, filter and scroll frame time).
**Related features.** F-010, F-011, F-014.

### AV-009 Metadata encoding and malformed tags
**Severity:** Minor
**Description.** ID3v2 frames with declared encodings that disagree with their contents, mixed Latin-1 and UTF-16 in one file, absent or contradictory duration fields, and embedded artwork large enough to matter. A player that trusts tags will display mojibake or, worse, produce a duration that makes seeking wrong.
**Detection.** Not implemented (would require a fixture set of deliberately malformed files with expected-output assertions).
**Related features.** F-010.

### AV-010 Unavailable media paths
**Severity:** Minor
**Description.** Playlists outlive the files they reference. Removable drives, unmounted network shares, broken symlinks and renamed directories all produce entries that cannot be played. The failure mode to avoid is stalling: an unavailable file must be reported and skipped, not retried indefinitely, and a network share that is slow rather than absent must not block the UI thread while it decides.
**Detection.** Not implemented (would require a fixture using a deliberately slow or absent mount point, asserting a bounded timeout and playlist advance).
**Related features.** F-001, F-010.

---

## Process

### AV-013 Documentation drift
**Severity:** Minor
**Description.** This document set specifies a system that does not exist yet. Every constant marked provisional in SPEC.md, every module in ARCHITECTURE.md, and every phase in ROADMAP.md is a prediction. The failure mode is not that predictions turn out wrong — that is expected — but that they are silently left in place after reality diverges, at which point the documents actively mislead.
**Detection.** Manual review, per Maintenance Rule 1 and Rule 7: the status header's Last reviewed date is refreshed on any session after a gap of more than two weeks, and a code change that invalidates documentation without updating it is treated as an incomplete commit. Cross-reference integrity between documents is currently unenforced; a checking tool would be the mechanical fix.
**Related decisions.** D-011.
