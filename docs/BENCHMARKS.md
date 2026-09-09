# Benchmarks

Every measured claim the project makes, in one place, taken from one tree on one
day.

**They were scattered before this file existed.** Frame times lived in
FEATURES.md, streaming-thread figures in ATTACK_VECTORS.md, the click
measurement in a feature status and the tail-loss figure in a bug entry — each
recorded when the work was done, on whatever the machine was doing that
afternoon, and never afterwards compared with the others. Numbers gathered that
way drift apart without anybody noticing, and two of them had: see §What this
file corrected.

**Baseline:** commit `d4d6cf9`, 2026-09-09, Release build.
**Hardware:** ThinkPad P15 Gen 2i — i7-11850H, NVIDIA T1200, 1920×1080 display.
This is BUILD.md's reference machine and it is also the development machine, so
these are measurements of the product on the hardware it was written on rather
than on a rig chosen to flatter it.
**Conditions:** a live desktop. Cinnamon running, a compositor running, an
editor open. Not an idle machine, deliberately — nobody uses one.

---

## How to read the frame figures

**Ranges, not single numbers.** The frame sweep was run three times with a
minute between, and what is quoted is the spread. A single run of this on a
shared GPU is not a measurement of a shader; it is a measurement of a shader and
whatever else wanted the GPU that second. The means are stable to about ±3%, and
the spread is the honest width of the claim.

**The verdict is less stable than the numbers.** One of the three runs failed:
flame took a single frame at 26.6 ms out of six hundred, against a mean of
9.271 ms and 44.4% headroom. The tool marks that MISSED. Its own closing note
says a lone spike is not attributable to the shader, so the note and the verdict
disagree — recorded as IMP-013 rather than adjusted here, because a benchmarks
file should not be where a tool's pass rule gets quietly loosened.

---

## Meter rendering — AV-002, F-031, F-032, F-033

`tools/measure-frames.sh 4`, offscreen pass, `glFinish` after each frame, so the
interval is the true cost of producing one. Budget is 16.67 ms; AV-002 requires
at least 30% spare.

### 3840×2160, three runs

| mode | mean frame | headroom | verdict |
|---|---|---|---|
| spectrum | 6.99 – 7.28 ms | 56.3 – 58.1% | holds |
| spectrum-mirror | 7.36 – 7.56 ms | 54.6 – 55.8% | holds |
| ladder | 7.53 – 7.73 ms | 53.6 – 54.8% | holds |
| flame | 8.87 – 9.27 ms | 44.4 – 46.8% | holds; one late frame in one run |
| **vu** | **9.84 – 10.27 ms** | **38.4 – 41.0%** | holds — the worst mode |

### 1920×1080

| mode | mean frame | headroom |
|---|---|---|
| spectrum-mirror | 2.03 ms | 87.8% |
| spectrum | 2.16 ms | 87.0% |
| ladder | 2.54 ms | 84.8% |
| flame | 2.68 ms | 83.9% |
| vu | 2.71 ms | 83.7% |

Four times the pixels costs between three and four times the frame, which is
what a fragment-bound workload should do and is the check that these are
measuring the shaders rather than something fixed.

### Switching between them — F-033

`tools/verify-mode-switch.sh`, vsync **on**, because a dropped frame only exists
where something is pacing them.

| | |
|---|---|
| switches in 20 s | 51 |
| dropped frames inside the 100 ms after any switch | **0** |
| dropped frames over the whole run | 0, against 0 steady |
| worst interval | 20.55 ms switching, 21.09 ms steady |
| sink underruns | 0 |

The attribution is the point. This machine drops one or two frames in twenty
seconds while idle, so counting a run's drops cannot answer whether switching
caused one; a frame lost to a switch lands within a frame or two of it.

---

## Audio path

### The streaming thread — AV-001

`tools/stress-audio.sh`, idle then under one spinning thread per core with
continuous disc I/O. Budget for the application's own work is 1 ms, an order of
magnitude under `pulsesink`'s 10 ms segment.

| | idle | under load |
|---|---|---|
| the application's own work | 8.2 µs worst, 2.0 µs mean | **23.4 µs worst, 3.5 µs mean** |
| the `g_object_set` handover | 1587 µs worst, 754 µs mean | **2660 µs worst, 1038 µs mean** |
| `pulsesink` underflows | 0 | **0** |

Ours is three orders of magnitude inside budget under full load. The
milliseconds belong to `playbin3`'s handover, which is not application work but
the mechanism `about-to-finish` exists to be answered by — and nothing in this
project bounds it, which is IMP-012.

### Transport response — F-002

Every command, measured as the time the call takes to return. The promise is
that a transport must not block; `gst_element_set_state` can be made to.

| play | pause | next | previous | stop |
|---|---|---|---|---|
| 0 ms | 0 ms | 0 ms | 0 ms | 1 ms |

### Equaliser — F-020, AV-003

A 1 kHz tone captured through the real elements while band 4 is taken to +12 dB,
measuring the high-frequency content of each 5 ms block against the tone it
rides on. A click is energy where a smooth gain change puts none.

| | worst 5 ms, relative to the tone |
|---|---|
| through the 30 ms ramp | **−103.2 dB** |
| written straight to the element | −98.2 dB |
| the capture's own floor | −117.2 dB |

Sixty decibels down is inaudible in a quiet room. Both are far below that, which
is the finding: `equalizer-nbands` does not click even unramped, because a gain
change moves biquad coefficients while the filter state carries across. Bypass
is **bit-identical** to the unprocessed signal.

### Meter ballistics — AV-012, AV-011

| | measured | required |
|---|---|---|
| 99% deflection | **302.0 ms** | 300 ms ±5%, IEC 60268-17 |
| overshoot | **1.16%** at 401 ms | 1 – 1.5% |
| tone at each display band's own bin peaks in that band | **20 of 20** | all |

The overshoot is not a tolerance being met but a shape being proved: a
first-order system cannot overshoot at all, so the figure is what says the
ballistic is second-order.

### Gapless — F-005

The pipeline never returns to `Stopped` across a join, and the handover does not
re-load the source. Resident memory over a hundred load-play-stop cycles:
**63.0 MB → 63.6 MB, 560 kB of growth.**

---

## Rendering correctness

### The render thread — AV-007

`tools/verify-render-thread.sh`, `QSG_RENDER_LOOP=threaded` forced, both RHI
backends, 12 seconds each.

| backend | uploads | on the GUI thread | threads distinct | validation |
|---|---|---|---|---|
| OpenGL | 680 | **0** | yes | silent |
| Vulkan | 692 | **0** | yes | silent |

### Scaling — AV-005

`tools/verify-scaling.sh`, a 626×330 logical panel.

| ratio | captured | expected | edge rise |
|---|---|---|---|
| 1.0× | 626×330 | 626×330 | 2 px |
| 1.5× | 938×494 | 939×495 | 2 px |
| 2.0× | 1252×660 | 1252×660 | 2 px |
| 3.0× | 1878×990 | 1878×990 | 2 px |

Edge rise is the 10–90% transition across a chassis-to-well boundary, in device
pixels. It stays at 2 px because the chrome is geometry rather than a resampled
image. A bitmap skin would show it growing with the ratio, which is the whole of
AV-005 and the reason the project exists.

---

## Known costs

| | measured |
|---|---|
| a next entry recognised but undecodable costs the track before it | **1.45 s** of its tail (BUG-027) |
| the gapless handover, on a streaming thread | 1.6 – 2.7 ms, unbounded by anything here (IMP-012) |

---

## What this file corrected

Collecting the numbers found two that had drifted, both of them flattering.

**The VU mode's headroom.** ATTACK_VECTORS.md recorded a range of 46.2% to 60.1%
after BUG-030 added eleven marks to the VU face, and said the mode had come out
*better* than the 40.6% it managed before the marks existed. Three runs today
put it at 38.4% to 41.0%. The 46.2% was a single run on a cool machine, and the
comparison it was used for — before against after — was between two single runs
in different thermal states rather than a measurement of the change. The mode
still passes AV-002's 30% floor comfortably. The claim that the marks made it
faster does not survive, and has been withdrawn.

**F-031's spectrum figures.** Recorded as 6.721 ms and 59.7% headroom on
2026-09-07. Today: 6.99 – 7.28 ms and 56.3 – 58.1%. The same shape of error, one
run quoted as though it were the number.

Neither changes any verdict. Both are the reason this file quotes ranges.
