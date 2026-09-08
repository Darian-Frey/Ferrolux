# Attack Vectors

Project-specific failure modes the project must be resilient against.
Grouped by category. Each vector lists detection method and severity.
Severity: Critical (must hold) | Major (regression on release blocks) | Minor (track only).

This file was written before the code, when every detection entry said `not implemented`. That was honest signal rather than a gap to be papered over: the distance between an identified failure mode and a working check for it is information, and it is worth reading which vectors closed early and which are still open.

Phase 7 requires every Critical vector to have implemented detection before RS-1 ships. **All four now have it** — AV-001, AV-003, AV-005 and AV-007, the last closing on 2026-09-08.

Two entries were found to be describing themselves wrongly while that was being finished: AV-011 and AV-012 both read `not implemented` when `meters_test` had been checking exactly what they asked for since Phase 4. A status nobody re-reads is a status that stops being true quietly, which is the same failure this file is about, one level up.

---

## Audio correctness

### AV-001 Application work on the GStreamer streaming thread
**Severity:** Critical
**Description.** Any allocation, lock acquisition against UI state, blocking I/O or synchronous cross-thread signal on a streaming thread starves the audio path. The symptom is intermittent dropouts under system load rather than a reproducible failure, which makes it expensive to find later and cheap to prevent now. Bus message handlers, metadata callbacks and probe functions are the likely entry points.
**Detection.** **Implemented**, in the two halves this entry asked for.

*The annotation pass* is a section of `spec_test`, which reads `src/`. It
collects every GStreamer signal the project connects and holds that set against
an inventory naming the thread each one runs on, so a new callback fails the
suite until somebody classifies it — the point being that the expensive thing
about this vector is not writing bad code on the audio path but arriving on the
audio path without noticing. It then brace-matches the body of the one callback
that does run on a streaming thread, `about-to-finish`, and checks it is written
out of seventeen constructs: signal emission, logging, allocation, blocking
cross-thread calls, file I/O, pipeline state changes and queries. It also
asserts no `gst_bus_set_sync_handler` exists anywhere, which is the single most
likely way this invariant would be broken, because it is what the documentation
reaches for when a bus message needs to be seen sooner.

Both checks were confirmed to fail before being trusted, by adding a `qWarning`
to the callback and an unclassified `g_signal_connect` beside it.

*The stress harness* is `tools/stress-audio.sh`. It plays twenty three-second
tracks — short so that `about-to-finish`, which fires once per track, is
actually exercised — first on an idle machine and then under one spinning thread
per core plus continuous disc I/O, and counts two things: how long the callback
holds a streaming thread, timed from inside it by `core/StreamTimer`, and how
many times `pulsesink` reported an underflow. The second comes from the sink's
own logging rather than from a probe, deliberately: a probe installed to detect
starvation would be one more piece of application work on the thread under
suspicion.

**Measured 2026-09-07** on the reference hardware, 16 spinning threads:

| | idle | under load |
|---|---|---|
| the application's own work | 7.4 µs worst, 3.5 µs mean | **4.6 µs worst, 1.8 µs mean** |
| the `g_object_set` handover | 2018 µs worst, 1035 µs mean | **4238 µs worst, 1224 µs mean** |
| `pulsesink` underflows | 0 | **0** |

**The two are separated because they answer to different people, and the first
run of the harness proved why.** Timed together, the callback read 2.4 ms with
eight of fifteen calls over budget on an *idle* machine, which looks exactly
like the defect this vector describes. It is not. The application's own work
there — a mutex, a refcounted copy, an atomic store — is three orders of
magnitude inside the 1 ms budget and does not move under load. What costs
milliseconds is `g_object_set(playbin, "uri", ...)`, which is not application
work but the entire mechanism `about-to-finish` exists to be answered by, and
what `playbin3` does inside it is not this project's to shorten.

That cost is nonetheless real and it doubles under load, from 2.0 ms to 4.2 ms.
It is recorded rather than budgeted, and judged by the only thing that can judge
it: whether the device noticed. It did not, in either condition. See IMP-012 for
what would have to change if it ever does.

**Related decisions.** D-002 (GStreamer backend), D-005 (CPU-side ballistics).
**Related features.** F-001, F-005, F-030.

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

Re-measured 2026-09-07, after BUG-028 — F-036 had turned the shaders' literals
into `Visuals.*` bindings and left the benchmark with none, so everything
measured between the two dates drew with undefined parameters. Offscreen at
3840x2160 with the property bound: every mode still holds, between **40.6% and
61.3%** of the budget spare, worst mode then the VU at 9.897 ms. The requirement
is 30%.

Measured again 2026-09-08, after BUG-030 put eleven marks on the VU face. The
first run of that change reported the VU at 12.454 ms and **25.3%** — a fail,
because eleven marks were being tested for every fragment of the sweep rather
than for the thin band they are drawn into. With the loop confined, every mode
holds between **46.2% and 60.1%**, the VU at 8.961 ms: better than before the
marks existed. The flame reads 8.190 ms, within variance of the Phase 4 figure, which
is what establishes that the Phase 4 run was measuring the right thing.

**The detection found the defect it was written for.** On its first run that
could reach 4K, flame took 26.9 ms per frame — 37 fps — with 377 of 600 frames
late, and the level sweep then showed that the quiet passages nobody would think
to test were the expensive ones. See BUG-016; the shader is now 4.6 times faster
with a pixel-identical result.

**The sweep settles the GPU between modes**, and that is not padding. Six hundred
frames at 2160p heats it, and the mode measured next inherits the heat: run back
to back, the last of five reads about 40% slower than the first. That put a mode
below the headroom floor and looked exactly like a regression, until the same run
passed comfortably after ninety seconds idle. Only one display mode is ever shown
at a time, so no user reaches the fourth mode's shader with three others' work
still in the pipe — measuring that way reports the cost of the benchmark's own
sequence rather than of the shader.

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
**Detection.** **Implemented**, in two halves, and the dynamic half does force both RHI backends and the threaded loop as this entry asked.

*The static half* is a section of `spec_test`, which reads `src/meters/MeterTexture.cpp`. It guards the single line whose alteration causes the failure — the connection to `beforeSynchronizing` must be `Qt::DirectConnection`, so the slot runs on the thread that emitted the signal — and checks that nothing in the file is queued onto the GUI thread. It then brace-matches both halves of the split and holds them apart: the GUI-thread staging path must contain no `createTextureFromImage`, `QSGTexture`, `adopt` or `setFiltering`, and the render-thread path must be the only place in the file a texture is created.

*The dynamic half* is `tools/verify-render-thread.sh`. `meters/RenderThreadGuard` records the thread each `MeterTexture` was constructed on — the GUI thread, since QML instantiates items there — and then counts every upload and every staging call against it. The tool plays a tone so the meters have something to upload, and runs the application under `QSG_RENDER_LOOP=threaded` with `QSG_RHI_BACKEND` set to `opengl` and then `vulkan`, asking for the Vulkan validation layer by name.

**It refuses to pass on a run that could not have failed.** The first check of each backend is that the render thread and the GUI thread were seen to be *different*, because under the basic loop they are the same thread and every question about which one you are on is trivially satisfied. A green result obtained there is the precise false negative this vector describes, so the tool reports it as a failure rather than a pass. The second check is that textures were actually uploaded, for the same reason in the other direction: a silent run uploads nothing and violates nothing.

**Measured 2026-09-08** on the reference hardware, 12 seconds per backend:

| | uploads | on the GUI thread | threads distinct | validation |
|---|---|---|---|---|
| OpenGL | 721 | **0** | yes | silent |
| Vulkan | 694 | **0** | yes | silent |

**The detection was confirmed against the real defect, and the defect behaved exactly as this entry predicted it would.** Changing the connection to `Qt::QueuedConnection` — one word, the smallest expression of this violation — put **489 of 489 uploads on the GUI thread under OpenGL, where the application went on running normally**, and **segfaulted within a second under Vulkan**. That is "appears to work under some backends and crashes or corrupts under others", observed rather than argued, on one machine and one afternoon. A developer working under OpenGL and the basic loop would have shipped it.

Two lessons are recorded in CLAUDE.md. The tool's first version counted Qt's own line announcing that the validation layer was enabled, so asking for validation was itself the finding, on both backends; and a crash must be reported as a crash, since the first version blamed an unavailable backend for a segfault that was the vector happening.

**Related decisions.** D-004, D-005.
**Related features.** F-030, F-031, F-032.

### AV-011 Low-frequency band collapse in the spectrum display
**Severity:** Major
**Description.** The `spectrum` element produces linearly spaced bands. Mapping those directly onto a logarithmic display without sufficient analysis resolution puts the bottom two octaves — everything below roughly 200 Hz — into a single display bar, so bass content is invisible. This is why SPEC.md specifies 512 analysis bands feeding 24 display bands rather than requesting 24 from the element.
**Detection.** **Implemented** in `meters_test`, as the sweep this entry described: §logarithmic band mapping and §tone placement feed a tone at each display band's own bin and assert its maximum response falls in that band and no other — 20 of 20 at the last run. This entry said `not implemented` until 2026-09-08, by which time the checks had been passing since Phase 4; the status was stale rather than the coverage missing.
**Related decisions.** D-004.
**Related features.** F-031.

### AV-012 VU ballistics degenerating into a peak meter
**Severity:** Major
**Description.** A VU meter that responds instantaneously is a peak meter wearing the wrong face, and it reads immediately as fake to anyone who has used real hardware. The risk is not that the filter is written wrongly but that it is quietly bypassed — for instance by a smoothing change made to fix a spectrum problem being applied to both paths, since they share a source.
**Detection.** **Implemented** in `meters_test` §VU ballistics, to the figures this entry specified: 99% deflection at **302.0 ms**, inside the ±5% of 300 ms that IEC 60268-17 asks for, overshooting by **1.16%** — which is inside the 1–1.5% band and, as the check says in as many words, is something a first-order system cannot do at all, so the test would fail if the filter were ever quietly replaced by one. Like AV-011, this entry read `not implemented` until 2026-09-08 while the checks had been passing since Phase 4.
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
