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

*None.*

## Deferred

*None.*
