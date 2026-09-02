# Decisions

Append-only log of significant design decisions.
Each entry: D-NNN, with Decided and Recorded dates (ISO 8601), status, context, alternatives, decision, consequences, and reversal conditions.
Status vocabulary: Proposed | Accepted | Superseded by D-NNN | Deprecated.

---

### D-001 Qt 6 with QML for the interface
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-040, F-041, ARCHITECTURE.md §Module responsibilities, D-003, D-004

**Context.** The interface is the project's reason for existing, and it has two awkward requirements at once: heavily custom drawn chrome, and conventional list behaviour for a playlist of twenty thousand rows. Most toolkits are good at one of those.

**Options.**
- **A. raylib with an immediate-mode layer.** Familiar from Caustic and terra-siege, excellent for drawn interfaces, direct shader access. Rejected: a playlist needs scrolling, multi-select, drag reorder, native file dialogs, clipboard, IME and Unicode text layout, all of which would be hand-built.
- **B. GTK4 with custom drawing.** Good platform integration on Linux. Rejected: custom drawing is comparatively awkward, shader integration is not a first-class path, and the styling system fights bespoke chrome.
- **C. Qt Widgets with custom painting.** Mature list views, but `QPainter`-based drawing on a software raster path makes 60 fps meters at 4K difficult, and shader integration is bolted on.
- **D. Qt 6 with QML.** Chosen. Scene-graph rendering, first-class `ShaderEffect`, retained-mode declarative UI, `QAbstractListModel` with a virtualised `ListView`, and native dialogs.

**Decision.** Option D. Qt 6.5 or later, QML for all presentation, C++ for engine and models.

**Consequences.**
- Shader-driven meters are a supported path rather than an escape hatch.
- Resolution independence is largely inherited rather than engineered.
- Adds a substantial dependency; packaging is heavier than a raylib binary.
- Qt licensing constrains the project licence — see D-010.

**Reversal conditions.** Revisit if (a) QML scene-graph overhead makes the 60 fps target unreachable at 4K on the reference hardware even with trivial shaders, or (b) Qt's open-source licensing terms change in a way that makes the intended project licence untenable.

---

### D-002 GStreamer as the audio backend
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-001, F-030, D-005, AV-001, AV-006, SPEC.md §Pipeline

**Context.** The project needs broad format coverage, an equaliser, level and spectrum analysis, and gapless playback. Writing decoders is out of the question; the choice is which existing stack to build on.

**Options.**
- **A. miniaudio plus decoder libraries.** Minimal dependency footprint, full control of the sample path. Rejected: format coverage becomes the developer's problem, and analysis, EQ and gapless all have to be written from scratch.
- **B. libmpv.** One dependency, excellent format coverage, gapless and ReplayGain built in. Rejected for this project: it is designed as a player rather than a pipeline, and extracting per-band spectrum data for the meters means fighting the abstraction.
- **C. FFmpeg with direct PipeWire output.** Maximum control. Rejected: reimplements a pipeline framework badly, and ties the project to one audio server.
- **D. GStreamer.** Chosen. Format coverage through plugin sets, `equalizer-10bands` and `spectrum` and `level` as ready elements, `playbin3` for gapless, and output device abstraction that works with PipeWire, PulseAudio, ALSA and JACK.

**Decision.** Option D. GStreamer 1.20 or later with `base`, `good`, `bad` and `libav` plugin sets.

**Consequences.**
- Meters, EQ and gapless are configuration rather than implementation.
- Format support becomes a packaging question, not a code question.
- Plugin licensing must be tracked; some plugin sets carry terms that affect D-010.
- Bus-message latency becomes a design constraint for the meters. See AV-004.

**Reversal conditions.** Revisit if (a) bus-message jitter proves incompatible with smooth meter animation and cannot be fixed by interpolation, or (b) a plugin dependency forces a licence outcome that is unacceptable under D-010.

---

### D-003 Bespoke vector chrome, not Winamp skin compatibility
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-040, F-041, F-044, AV-005, FEATURES.md §Out of scope

**Context.** The project began as an intention to fork qmmp and fix its classic-skin scaling. Investigation of what "fixing" would mean established that the defect is not in any skin engine's implementation. Classic Winamp skins are fixed-size bitmaps authored for a 275×116 window. Any scaling is resampling, and resampling bitmap chrome either blurs it or produces nearest-neighbour blockiness. No engine can do better than the source material allows.

**Options.**
- **A. Fork qmmp and improve its skin scaling.** The original plan. Rejected: bounded above by the bitmap format itself; effort spent on a ceiling that is too low.
- **B. Support classic skins alongside a native theme.** Rejected: the classic path would still be bad, so it would attract the complaints while doubling the rendering surface.
- **C. Bespoke drawn chrome with no skin compatibility.** Chosen.

**Decision.** Option C. All chrome is vector geometry and shaders. Classic skin compatibility is explicitly out of scope, permanently, and the cassette futurism direction takes the place skinning would have occupied.

**Consequences.**
- Resolution independence is structural rather than a feature to be engineered.
- The project loses the existing skin ecosystem entirely, and with it the audience that wants it.
- Visual identity becomes the project's own responsibility, which is the intended trade.
- Theme variants (F-044) become token sets over shared geometry.

**Reversal conditions.** None expected. This decision defines the project; reversing it would produce a different project. If classic skin support is ever wanted, it belongs in a separate application.

---

### D-004 Shader-rendered meters fed by a texture, not uniform arrays
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-030, F-031, F-032, AV-002, AV-007, SPEC.md §Meters

**Context.** Spectrum and VU displays update at frame rate and are the visual centrepiece. The rendering path determines both quality and cost.

**Options.**
- **A. QML `Canvas`.** Familiar 2D API. Rejected: software-rasterised into a texture, expensive per frame, and antialiasing quality is poor for thin geometry like a needle.
- **B. `QQuickPaintedItem`.** Same rasterisation cost with a C++ API. Rejected for the same reason.
- **C. `ShaderEffect` with band values passed as uniform arrays.** Rejected: uniform array sizes are capped and awkward across backends, and per-frame uniform updates for many values are inefficient.
- **D. `ShaderEffect` reading an `N×1` two-channel texture uploaded per frame.** Chosen.

**Decision.** Option D. A `QQuickItem` owns an `N×1` RG16 texture; the red channel carries current magnitude and the green channel carries the peak-hold cap. Shaders read it with linear filtering.

**Consequences.**
- Per-fragment evaluation gives sub-pixel bar edges and free antialiasing on the needle.
- Linear filtering interpolates between bands at no cost.
- Logarithmic frequency remapping happens in the shader as a coordinate transform.
- Adds a small amount of C++ scene-graph code that must respect render-thread rules.

**Reversal conditions.** Revisit if texture upload cost measurably dominates the meter frame budget, or if a target backend proves unable to sample a two-channel 16-bit texture with linear filtering.

---

### D-005 Ballistics and smoothing on the CPU, rendering on the GPU
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-032, D-004, SPEC.md §Meters

**Context.** Meter behaviour has two separable parts: how values evolve over time (integration, decay, peak-hold) and how they are drawn. Either could live on either side of the CPU/GPU boundary.

**Options.**
- **A. All state on the GPU via ping-pong framebuffers.** Elegant, avoids per-frame upload. Rejected: state becomes untestable, and the amount of data involved makes the upload cost irrelevant anyway.
- **B. Ballistics on the CPU, rendering on the GPU.** Chosen.

**Decision.** Option B. Roughly sixty floats per frame of state evolution on the CPU, unit-testable in isolation; everything visual in the shader.

**Consequences.**
- VU ballistics can be tested against known reference behaviour without rendering anything.
- Per-frame upload cost is negligible at this data size.
- Display modes share one state source, so switching modes preserves meter continuity.

**Reversal conditions.** Revisit if band count grows by an order of magnitude, for example if a spectrogram mode (candidate) needs full history resident on the GPU.

---

### D-006 GStreamer's equaliser element for RS-1, behind an abstraction
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-020, F-021, F-022, AV-003, SPEC.md §Equaliser

**Context.** The ten-band equaliser can be the stock GStreamer element or a hand-written biquad cascade. The stock element is available immediately; a custom cascade gives exact control over the filter response and would be needed for faithful Winamp preset behaviour.

**Options.**
- **A. Custom biquad cascade from the start.** Full control, roughly eighty lines of DSP. Rejected as a starting point: it puts DSP debugging on the critical path before there is anything to listen to.
- **B. `equalizer-10bands` permanently.** Simplest. Rejected: forecloses exact `.eqf` preset compatibility and the parametric candidate feature.
- **C. `equalizer-10bands` behind a `core/Equaliser` abstraction that exposes decibel gains only.** Chosen.

**Decision.** Option C. Ship RS-1 on the stock element. The abstraction exposes bands and preamp in decibels and hides the backend entirely, so a custom cascade can replace it without touching the UI or the preset code.

**Consequences.**
- Phase 3 is short.
- `.eqf` import is a gain mapping in RS-1 rather than a faithful curve reproduction; the difference must be documented rather than hidden.
- The abstraction seam is load-bearing and must not leak backend properties.

**Reversal conditions.** Switch to a custom cascade if (a) the stock element's response proves audibly wrong against reference curves, (b) exact `.eqf` fidelity becomes a requirement rather than a convenience, or (c) the parametric equaliser candidate is promoted.

---

### D-007 Winamp band centres and a ±12 dB range
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-09-02)
**Related:** F-020, F-021, F-022, SPEC.md §Equaliser

**Context.** Band centre frequencies and gain range determine whether existing presets mean anything and whether the interface matches user expectation.

**Options.**
- **A. ISO octave centres (31.5 Hz to 16 kHz).** More textbook, but does not match the presets people already have.
- **B. Winamp's ten centres with ±12 dB.** Chosen.

**Decision.** Option B. Centres at 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000 and 16000 Hz, each ±12 dB, with a ±12 dB preamp. Exact values in SPEC.md §Equaliser.

**Consequences.**
- Existing `.eqf` presets map one to one.
- The bottom band at 60 Hz leaves sub-bass unaddressed, which is a genuine limitation of the original layout and is inherited deliberately.
- Slider labelling matches what a Winamp user expects to see.

**Reversal conditions.** Revisit if the parametric candidate is promoted, at which point the graphic layout becomes one preset shape among several rather than the fixed structure.

---

### D-008 RS-1 as a display badge, semantic versioning in the repository
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** README.md, CHANGELOG.md

**Context.** The project name carries a hardware-style model code. Model codes and software version numbers answer different questions and should not be conflated.

**Options.**
- **A. Model code as the version, incremented per release (RS-1, RS-2…).** Rejected: no way to express a patch release, and it breaks every packaging convention.
- **B. Semantic versioning only, model code dropped.** Rejected: loses the identity the name was chosen for.
- **C. Both, with defined roles.** Chosen.

**Decision.** Option C. Git tags, package versions and `CHANGELOG.md` use semantic versioning. RS-1 appears on the panel badge, the window title and the About box, and denotes the hardware generation rather than the software revision. A future substantial redesign becomes RS-2 regardless of where semver has reached.

**Consequences.**
- Packaging conventions are satisfied.
- The badge stays stable across patch releases, as a model code should.
- Requires a stated rule for when the model code increments; that rule is "a redesign of the panel or a change in the interaction model", recorded here.

**Reversal conditions.** None expected.

---

### D-009 Linux-first, no Windows or macOS target in RS-1
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** BUILD.md, F-051

**Context.** Qt and GStreamer are both portable. The question is not whether the code could run elsewhere but whether other platforms are supported, tested and packaged.

**Options.**
- **A. Cross-platform from the start.** Rejected: triples the testing surface and packaging work for a solo developer, before there is anything worth porting.
- **B. Linux only, portability not considered.** Rejected: gratuitous platform coupling is easy to avoid and expensive to undo.
- **C. Linux-first, portability preserved where free.** Chosen.

**Decision.** Option C. X11 and Wayland are both supported and tested. Platform-specific code is confined to `platform/`. No Windows or macOS build is offered, tested or packaged for RS-1.

**Consequences.**
- Testing surface stays manageable.
- `platform/` isolation means a future port is bounded work in one directory.
- Media key handling differs between X11 and Wayland and both paths need testing. See F-051.

**Reversal conditions.** Revisit after RS-1 ships if there is demonstrated demand and someone willing to maintain the additional packaging.

---

### D-010 Project licence
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-02)
**Related:** README.md, D-001, D-002, D-012, ROADMAP.md Phase 7, FEATURES.md §Future

**Context.** No licence has been chosen. This must be settled before the first public commit of source code. Two upstream dependencies constrain the choice. Qt 6's open-source distribution is offered under LGPLv3 and GPLv3; using it under LGPL imposes relinking obligations but does not dictate the application's own licence. GStreamer's core and base plugins are LGPL, but plugin sets vary, and some codecs distributed in the wider ecosystem carry terms that propagate.

**The constraint proved weaker than assumed.** Audited on 2026-09-02 against the Phase 1 build. The binary links only Qt 6 Base and Declarative — both offered under LGPLv3, GPLv2 *and* GPLv3, so a v2 route was open — plus glib, gobject and GStreamer core. Every plugin the pipeline uses — `playbin3`, `audioconvert`, `capsfilter`, `audiomixmatrix`, `equalizer-10bands`, `level`, `spectrum`, `autoaudiosink` and the decoders — reports LGPL. Ubuntu's `gst-libav` is built without `--enable-gpl` and ships as LGPL-2+. TagLib is LGPLv2.1/MPL, and the bundled fonts are OFL 1.1 per D-012. Nothing in the dependency set forces copyleft, so this was decided on merit rather than obligation.

**Options.**
- **A. GPLv3.** Consistent with the surrounding ecosystem, avoids any question about plugin propagation, matches what comparable players use. Cost: forecloses proprietary derivatives, including any the author might later want.
- **B. LGPLv3.** Suits libraries; ill-fitting for an end-user application.
- **C. MIT or Apache-2.0.** Maximum permissiveness. Requires care that no GPL plugin dependency is linked rather than loaded, and that Qt is used under LGPL terms with relinking preserved.
- **D. Dual licensing.** Rejected as disproportionate for a solo project with no commercial intent.

**Decision.** Option A, as **GPL-3.0-or-later**. `LICENSE` carries the unmodified GPLv3 text; every source file carries an `SPDX-License-Identifier: GPL-3.0-or-later` header and a copyright line.

Four reasons, in order of weight.

1. Permissive licensing buys an application almost nothing. It exists to maximise *library* adoption — people embed your code in their own product — and nobody embeds a music player. Permissive terms would give away the right to fork Ferrolux proprietary and return no adoption benefit for it.
2. Copyleft is *less* compliance work here, not more. ROADMAP.md Phase 7 requires a Flatpak, which bundles Qt. Under a permissive licence the LGPLv3 §4 relinking obligation for that bundled Qt still applies and must be discharged for every binary shipped. Under GPL, complete corresponding source is provided anyway and the obligation is met by construction.
3. The asset being protected is the design. D-003 makes visual identity the project's own responsibility and the deliberate trade for abandoning the skin ecosystem. The panel, the token set and the shader chrome are the work; copyleft keeps them from being shipped inside a closed product.
4. GPLv3 rather than GPLv2 for the explicit patent grant, and "or later" rather than v3-only so the project is not stranded on a single revision.

**Consequences.**
- The repository can carry source. The documentation-only restriction is lifted.
- Ferrolux cannot be incorporated into proprietary software by anyone else.
- Qt's LGPL relinking obligation is discharged automatically by the GPL source requirement, removing a recurring packaging burden from Phase 7.
- The author is not bound by his own licence and may relicense or dual-license at will **while sole copyright holder**. This is the answer to Option A's stated cost: what forecloses a future proprietary version is merging third-party contributions, not this decision.
- GPLv3 §6 requires Installation Information for "User Products", so a locked-down device build would not comply. This bears directly on the hardware companion candidate in FEATURES.md and is the one place choosing v3 over v2 costs something.

**Reversal conditions.** Reversal is possible only while the author remains sole copyright holder; once third-party contributions are merged it requires every contributor's consent. If the hardware companion candidate is promoted and would ship on a locked device, revisit *before* accepting any outside contribution, because that is the point of no return. A CLA or DCO policy should therefore be settled before the first external pull request rather than after it.

### D-011 In-repository bug and improvement catalogues
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** BUGS.md, IMPROVEMENTS.md

**Context.** The documentation standard makes `BUGS.md` and `IMPROVEMENTS.md` optional where an external tracker is in use. The repository is on GitHub, so Issues are available.

**Options.**
- **A. GitHub Issues only.** Standard practice, better for external reporters. Rejected as the primary record: the workflow here is solo development with an AI partner, and an AI session cannot reliably consult or update an out-of-repo tracker mid-task.
- **B. In-repo catalogues only.** Chosen for the development record.
- **C. Both, with defined roles.** Effectively what results.

**Decision.** Option B for the development record, with GitHub Issues remaining open for external reports. Anything reported externally that turns out to be real is mirrored into `BUGS.md` with a link. Maintenance Rule 8 — log when found, not silently acted on — applies to both files and is the reason they exist.

**Consequences.**
- Bug and improvement history survives forge migration and is readable offline.
- Requires discipline to mirror external reports; a stale mirror is worse than none.
- AI development sessions have the catalogues in context by default.

**Reversal conditions.** Revisit if external contribution volume makes manual mirroring the dominant maintenance cost.

---

### D-012 Four OFL-licensed faces, one per type role
**Decided:** 2026-09-02
**Recorded:** 2026-09-02
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-02)
**Related:** F-040, F-041, F-044, D-003, D-010, AV-005, SPEC.md §Design tokens

**Context.** The readout faces carry more of the cassette futurism identity than any other single element of the panel, so typography is a structural choice rather than a styling one. Two constraints bound it simultaneously. The faces must be redistributable inside a packaged application, because D-003 makes the drawn panel the product and an appearance that depends on what the user happens to have installed is not an appearance the project controls. They must also stay correct at fractional device pixel ratios, because AV-005 is the defect the project exists to avoid.

The font collection initially assembled for the project failed the first constraint entirely. Of seven archives, five were freeware for non-commercial use only, one was personal-use-only with a commercial licence sold separately, one was a demo cut of a commercial family, and the remaining one forbade distribution as part of a compilation even at zero price. None could be bundled. This was established before any of them was committed, which is the only reason it was cheap.

**Options.**
- **A. System fonts via fontconfig.** No licensing burden and no repository weight. Rejected: the panel would look different on every distribution, there is no system equivalent of a seven-segment or dot-matrix readout face to fall back to, and F-041's acceptance criterion — pixel-crisp screenshots at every tested scale factor — cannot be verified against a face the project does not control.
- **B. Keep the original collection, ship without it, ask users to install the fonts.** Rejected: it makes the project's defining visual identity an optional extra, and the licences prohibit redistribution regardless, so it relocates the problem to the user rather than solving it.
- **C. Draw the segment and matrix geometry directly as vectors, with no text face for the readouts.** The strongest rejected option, and coherent with D-003 — a seven-segment display *is* vector geometry, and drawing it would give exact control with no licence question at all. Rejected as a starting point: it puts segment geometry, a character map and spacing logic on the critical path to solve a problem a font already solves, and it addresses only the readouts, leaving the chassis legends still needing a face.
- **D. Four faces under the SIL Open Font License, one per type role.** Chosen.

**Decision.** Option D. `type-readout-numeric` is DSEG7 Classic, `type-readout-text` is Handjet, `type-readout-segment` is DSEG14 Classic, and `type-legend` is IBM Plex Sans Condensed. Exact roles, Handjet axis values and the ghost-layer rendering requirement are in SPEC.md §Design tokens.

OFL 1.1 or an equivalently redistributable licence is a hard requirement for any face on the control surface. Free of charge is not sufficient and must not be mistaken for it; that mistake is what produced the original collection.

**Consequences.**
- Bundling is unconditional and survives any outcome of D-010, in a Flatpak and in a `.deb` alike. The obligations are to ship the licence text and to leave reserved font names alone if the glyphs are modified.
- The repository gains binary assets. These are the only binaries in the project, which is worth preserving as a property.
- Handjet is variable, so the dot-matrix character is token data rather than a choice of asset. This keeps F-044 a token swap. **Amended 2026-09-02:** this consequence does not hold. Runtime axis control needs Qt 6.7, which the target distributions do not ship, so a static instance is bundled instead and the axis values are fixed at generation time. See BUG-001 and SPEC.md §Design tokens. The decision itself stands; only this consequence was wrong.
- The two-layer ghost rendering is a requirement rather than a style, and must survive refactoring of the readout components.
- The project depends on four upstreams that could go dormant. The exposure is bounded: OFL grants are irrevocable, so a dormant upstream costs future improvements, not present rights.
- The pixel and bitmap-revival category is excluded from the control surface. It is the most period-correct category available, so this is a real aesthetic cost, paid deliberately to AV-005.
- Packagers may prefer to depend on distribution font packages rather than the bundled copies. The build should tolerate an unbundled configuration rather than assuming its own copies are present.

**Reversal conditions.** The role structure is the decision; the specific faces are replaceable within it, and substituting one is not a reversal. Revisit the decision itself if (a) a readout face proves unable to render a script or symbol the display needs, or (b) per-segment behaviour becomes wanted — individual segment fade, or a segment failing as an affectation — which a text face cannot express at all. Option C is the reversal path in both cases, and the ghost-layer requirement is the point at which its cost stops looking disproportionate.
