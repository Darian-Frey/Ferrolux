# Bugs

Catalogue of bugs discovered during development. Per the project workflow,
bugs are **logged here when found, not silently fixed** (see Maintenance
Rule 8). The author decides whether to fix immediately, defer, or leave
alone.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

IDs are append-only. Entries move between the sections below as their
status changes; the `Status:` field is the source of truth.

See DECISIONS.md D-011 for why this catalogue lives in the repository
rather than in GitHub Issues. Externally reported bugs that prove real
are mirrored here with a link back to the issue.

---

## Open

*None.*

## Fixed

*None.*

### BUG-004 SPEC.md names an equaliser element that cannot meet D-007
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, starting Phase 3
**Related:** F-020, D-006, D-007, SPEC.md §Pipeline, SPEC.md §Equaliser

SPEC.md §Pipeline and ARCHITECTURE.md §Data flow both name
`equalizer-10bands`. That element's band centre frequencies are fixed at 29,
59, 119, 237, 474, 947, 1889, 3770, 7523 and 15011 Hz and are not settable —
`band0` through `band9` are gain-only properties. D-007 fixes the centres at
Winamp's 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000 and 16000 Hz, and
is Accepted specifically so that existing `.eqf` presets map one to one. The
named element cannot satisfy the accepted decision.

`equalizer-nbands` with `num-bands=10` is the same filter implementation
exposed through `GstChildProxy`, giving ten child bands each with settable
`freq`, `bandwidth`, `gain` and `type`. Verified against the installed
GStreamer 1.24.2. It is still a stock element, so D-006 is unaffected — only
SPEC.md's naming is wrong.

Phase 3 is implemented against `equalizer-nbands`. SPEC.md §Pipeline,
SPEC.md §Equaliser and ARCHITECTURE.md §Data flow need amending to match, and
the per-band bandwidths D-007's uneven layout implies need specifying, since
the ten Winamp centres are not octave-spaced and the top three sit close
together.

**Resolved 2026-09-02.** Phase 3 is implemented against `equalizer-nbands` with ten child bands. SPEC.md §Pipeline, its element configuration table and ARCHITECTURE.md §Data flow now name it, and SPEC.md §Equaliser gains the per-band bandwidth table that the uneven Winamp layout requires. D-006 is unaffected: this is still a stock element behind the same abstraction.

### BUG-005 The headroom rule is insufficient and does not prevent clipping
**Status:** fixed
**Severity:** high
**Found:** 2026-09-02, first run of the AV-003 detection in `tests/equaliser_test`
**Related:** F-020, F-021, AV-003, D-007, SPEC.md §Equaliser

SPEC.md §Equaliser specifies an automatic attenuation of
`max(0, preamp_dB + max_band_dB − 0 dBFS_margin)`. It does not hold. The rule
assumes the cascade's worst-case gain equals its largest single band gain, but
ten peaking filters in series multiply where their skirts overlap, and the
Winamp centres overlap substantially.

**Measured.** A near-full-scale sawtooth through the implemented chain with all
ten bands at +12 dB and the preamp at +12 dB. The rule removed 24 dB. Output
peaked at **2.5233, or +8.04 dBFS** — clipped by more than 8 dB. Nothing was
infinite or NaN, so the filters stayed stable, but the signal did not.

Modelling the cascade independently, as RBJ peaking sections at the centres and
bandwidths in use, puts its peak at **+21.37 dB at 607 Hz** for that curve, not
+12 dB. Combined with a +12 dB preamp the true requirement is 33.4 dB of
attenuation, against the 24 dB the rule asks for — a 9.4 dB shortfall, which
matches the measured 8.04 dB overshoot once the sawtooth's own spectrum is
accounted for.

This is exactly the failure AV-003 was written to anticipate, found by the
detection AV-003 said was needed. The vector's severity of Critical is
justified.

**Candidate resolutions**, for the author to choose:
1. Compute the cascade's actual peak magnitude response and attenuate by that:
   `max(0, preamp_dB + 20·log₁₀(max|H(f)|))`, evaluated over log-spaced
   frequencies whenever a gain changes. Exact, cheap — a few hundred complex
   evaluations, once per slider move, not per sample. The cost is that the
   headroom model must know the filter topology, so it becomes something that
   travels with the backend rather than being backend-agnostic.
2. Attenuate by the sum of the positive band gains. Trivially safe and needs no
   model, but the worst case removes 120 dB for a curve that only asks for a
   fraction of it, which would make the equaliser unusable.
3. Leave the rule and add a limiter before the sink. Changes the sound under
   load rather than preventing the condition, and SPEC.md is explicit that the
   attenuation is not user-visible — a limiter audibly is.

Resolution 1 is recommended. Whichever is chosen, SPEC.md §Equaliser's formula
needs replacing, and the AV-003 detection entry should be updated from
`not implemented` to reference `tests/equaliser_test`.
**Resolved 2026-09-02.** Candidate 1 adopted. `Equaliser::cascadePeakGain` models the curve as RBJ peaking sections and evaluates the cascade's magnitude over 1024 log-spaced frequencies, and the headroom rule now attenuates by `max(0, preamp + cascade_peak)`. Modelled at 192 kHz, which is conservative at every lower rate and avoids depending on the negotiated one. Re-measured: the same worst-case curve now peaks at 0.657, or −3.65 dBFS, against 2.523 before. SPEC.md §Equaliser carries the corrected formula and the reasoning, and AV-003's detection entry now reads implemented.


### BUG-001 Documented Qt minimum is unattainable, and SPEC.md's Handjet axes need Qt 6.7
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, during Phase 1 environment setup
**Related:** D-012, SPEC.md §Design tokens, BUILD.md §Supported platforms, AV-013

BUILD.md gives Qt 6.5 as the minimum. Ubuntu 24.04 LTS ships Qt 6.4.2 and
has no later version in its repositories, and Linux Mint 22 — the
reference platform named in BUILD.md — is derived from it. The project's
own reference hardware therefore cannot meet the project's own documented
minimum from its distribution packages.

Separately and more seriously, SPEC.md §Design tokens specifies Handjet
axis values (`ELSH` 8.0, `ELGR` 1.0, `wght` 500) as `ferric` token data.
Setting a variable font axis at runtime requires `QFont::setVariableAxis`,
introduced in **Qt 6.7**. The installed Qt 6.4.2 headers contain no
variable-axis API at all, so on that version Handjet renders at its
default instance — `ELSH` 2.0, a square element — and the readout is
square-dot rather than round-dot, silently and with no error.

Neither issue blocks Phase 1, which uses no Qt feature newer than 6.2.
Both come due at Phase 5.

**Candidate resolutions**, in preference order, for the author to choose:
1. Bundle a *static instance* of Handjet generated at the specified axis
   values with `fonttools varLib.instancer`, removing the runtime API
   requirement entirely and keeping the distribution Qt. Costs the
   "axes are token data" property claimed in SPEC.md, and requires
   respecting the OFL Reserved Font Name clause on the modified file.
2. Raise the toolchain minimum to Qt 6.7 and source Qt outside the
   distribution. Costs packaging simplicity and contradicts the
   Linux-distribution-native posture implied by ROADMAP.md Phase 7.
3. Substitute a static dot-matrix face for `type-readout-text`. Permitted
   without reversing D-012, whose reversal clause makes the role
   structure the decision and individual faces replaceable.

Whichever is chosen, BUILD.md's stated minimum must be corrected to match
reality rather than left as an aspiration.

**Resolved 2026-09-02.** BUILD.md now states Qt 6.4 as the minimum and CMakeLists.txt enforces it; the verified-configuration table records the correction explicitly. The variable-axis half is resolved by candidate 1: SPEC.md §Design tokens now specifies a static Handjet instance generated with `fonttools varLib.instancer`, which removes the Qt 6.7 requirement altogether. D-012's consequence claiming the axes are token data has been amended in place, since it no longer holds. Generating and bundling the instance is Phase 5 work gated on D-010, tracked by the specification rather than by this entry.

### BUG-002 SPEC.md specifies a balance law but no element to compute it
**Status:** fixed
**Severity:** low
**Found:** 2026-09-02, implementing F-004 in Phase 1
**Related:** F-004, SPEC.md §Pipeline, SPEC.md §Volume taper, ARCHITECTURE.md §Data flow

SPEC.md §Volume taper gives balance an exact constant-power law, but the
pipeline in SPEC.md §Pipeline — `audioconvert`, `equalizer-10bands`, `level`,
`spectrum`, `audioconvert` — contains nothing that could apply it, and the
chain in ARCHITECTURE.md §Data flow matches. The formula has no home.

Phase 1 fills the gap with an `audiomixmatrix` element carrying a diagonal
matrix, placed in the audio-filter bin ahead of where the equaliser will go. A
diagonal mix matrix is precisely a per-channel gain, so it computes the
specified law exactly. `audiopanorama` was rejected: its "simple" mode scales a
single channel and its "psychoacoustic" mode applies a model of its own, and
neither is the law SPEC.md gives.

This placement is an implementation choice filling a documentation hole, not a
specified design. It needs a ruling and a SPEC.md amendment fixing the element's
identity and its position relative to the equaliser and the analysis elements.
The latter matters: anything upstream of `level` and `spectrum` becomes visible
in the meters.

**Resolved 2026-09-02.** SPEC.md §Pipeline and ARCHITECTURE.md §Data flow now both name `audiomixmatrix` and place it after the equaliser and before `level` and `spectrum`, so the meters show the balance as heard — consistent with the reason the equaliser already sat there. The required `capsfilter` and the set-matrix-before-linking constraint are documented alongside it, and SPEC.md's element configuration table gained rows for both. `core/Engine.cpp` matches.

### BUG-003 The balance law is a mono panning law, and costs 3 dB at centre
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, verifying F-004 against SPEC.md
**Related:** F-004, BUG-002, SPEC.md §Volume taper, AV-003

SPEC.md §Volume taper specifies `left = cos((b+1)×π/4)`, `right = sin((b+1)×π/4)`
and justifies it as constant power, "so that a centred image does not lose
perceived loudness relative to a hard-panned one". Applied as written, it does
the opposite of its own stated goal.

That formula is the constant-power law for **panning a mono source** into a
stereo field. Its constant-power property depends on the *same* signal reaching
both channels, so that `L² + R² = 1` describes one source's total power. Ferrolux
applies it as a **balance** control, scaling the two channels of an already-stereo
signal independently, where that identity no longer means anything.

The measured consequence:

| balance | L gain | R gain | L dB | R dB |
|---------|--------|--------|------|------|
| −1.0 | 1.0000 | 0.0000 | 0.00 | −∞ |
| −0.5 | 0.9239 | 0.3827 | −0.69 | −8.34 |
| 0.0 | 0.7071 | 0.7071 | −3.01 | −3.01 |
| +0.5 | 0.3827 | 0.9239 | −8.34 | −0.69 |
| +1.0 | 0.0000 | 1.0000 | −∞ | 0.00 |

Centred playback is 3.01 dB quieter than the source on both channels, and moving
the control to hard left raises the left channel by 3 dB relative to centre. A
balance control that makes a channel louder when moved off centre is wrong in the
way a user will notice immediately, and it is precisely the loudness discrepancy
the specification's own rationale set out to avoid.

**Candidate resolutions**, for the author to choose:
1. Conventional attenuate-only balance: `left = min(1, 1−b)`, `right = min(1, 1+b)`.
   Centre is unity on both channels and the control can only ever cut, which also
   removes it as a clipping contributor under AV-003. This is what hardware
   balance controls do and what the rationale in SPEC.md describes wanting.
2. Keep the current law but normalise so centre is unity — multiply both by √2.
   Restores centre loudness but makes hard-pan gain 1.414, adding a clipping path
   that AV-003 would then have to account for.
3. Keep as specified and document the 3 dB centre attenuation as intended, on the
   grounds that it can never exceed unity and the volume control recovers it.

Resolution 1 is recommended. Whichever is chosen, SPEC.md §Volume taper needs its
formula and its rationale corrected together — the rationale is currently a
description of behaviour the formula does not produce.
**Resolved 2026-09-02.** Candidate 1 adopted. `Engine::balanceGains` implements `left = min(1, 1−b)`, `right = min(1, 1+b)`, and SPEC.md §Volume taper carries the corrected formula together with a rationale that now describes what the formula actually does. Both gain laws were extracted as pure static functions and are covered by eight checks in `tests/acceptance_transport`, including that no balance position exceeds unity gain. That coverage is the real fix: the original defect survived the first acceptance run only because nothing exercised balance at all.

## Won't fix

### BUG-006 `equalizer-nbands` advertises controllable band gains but never syncs them
**Status:** wontfix
**Severity:** low
**Found:** 2026-09-02, implementing the gain ramp for F-020
**Related:** F-020, D-006, SPEC.md §Equaliser

Each band of `equalizer-nbands` exposes `gain` with GStreamer's `controllable`
flag, and `gst_object_add_control_binding` accepts a
`GstDirectControlBinding` over it without error. The binding then does nothing:
`GstIirEqualizer` never calls `gst_object_sync_values` on its child bands while
streaming, so the bound control source is never evaluated.

Verified both ways against GStreamer 1.24.2. A linear interpolation control
source bound to band 0 and driven through a playing pipeline left the gain at
0.000 for the whole run. The identical source synchronised by hand with
`gst_object_sync_values` interpolated exactly — 0.000, 3.000, 6.000, 9.000,
12.000 across the requested 300 ms. The control machinery works; the element
simply never asks it for a value.

This matters because a control source is the obvious and documented way to ramp
a gain in GStreamer, it attaches without complaint, and it fails silently. The
next person to implement smoothing here will reach for it first.

**Not ours to fix** — the defect is upstream, and D-006 commits RS-1 to the
stock element. Ferrolux works around it by interpolating on the application
thread: `Equaliser` steps the property from a 5 ms timer and computes the
fraction from a clock rather than from a tick count, so timer jitter cannot
stretch or shorten the 30 ms specified in SPEC.md. The element re-reads the gain
once per buffer regardless, so driving it faster than that would buy nothing.

Revisit if the parametric equaliser candidate is promoted and D-006's reversal
conditions bring a hand-written cascade into scope, at which point the ramp
belongs inside the filter and this element stops being involved.

## Deferred

*None.*
