# Specification

Authoritative technical reference for Ferrolux RS-1. Constants defined here are not duplicated elsewhere; other documents link to this one.

Values marked **provisional** have been chosen from reference behaviour but not yet validated against an implementation. They are expected to be confirmed or adjusted during the phase that implements them, at which point the marking is removed.

---

## Pipeline

The audio chain, in order:

```
playbin3
  audio-filter = bin(
      audioconvert
    → volume          preamp, with the headroom attenuation folded in
    → equalizer-nbands  num-bands=10
    → capsfilter      audio/x-raw,channels=2
    → audiomixmatrix  balance
    → level
    → spectrum
    → audioconvert
  )
  audio-sink = autoaudiosink
```

`level` and `spectrum` are pass-through analysis elements and do not alter the signal. Both sit after the equaliser **and after the balance element** so that the meters display the signal as heard rather than as decoded. A hard-left balance therefore drops the right VU needle to zero, which is the behaviour of the hardware being modelled.

`audiomixmatrix` carries a diagonal matrix and so applies a per-channel gain, which is exactly the balance law in §Volume taper. `audiopanorama` is not used: its `simple` mode scales a single channel and its `psychoacoustic` mode applies a model of its own, and neither computes the specified law.

The `capsfilter` is not decoration. `audiomixmatrix` declares `channels: [1, MAX]` on both pads, so nothing else in the chain pins the channel count, and a mono source would negotiate one channel against an element configured for two — failing at `set_caps` with `Erroneous matrix detected` rather than at link time. Forcing stereo makes the upstream `audioconvert` up-mix and guarantees the matrix matches. The element also refuses to link at all while its `matrix` property is empty, so the matrix must be set before the chain is linked.

**Element configuration:**

| Element | Property | Value | Notes |
|---------|----------|-------|-------|
| `level` | `interval` | 16000000 ns (16 ms) | Approximately one message per frame at 60 fps |
| `level` | `peak-ttl` | 1500000000 ns (1.5 s) | Peak indicator hold |
| `level` | `peak-falloff` | 20.0 dB/s | Peak indicator decay |
| `spectrum` | `bands` | 512 | Analysis resolution, not display resolution |
| `spectrum` | `interval` | 16000000 ns (16 ms) | Matched to `level` |
| `spectrum` | `threshold` | −80 dB | Floor of the reported magnitude range |
| `spectrum` | `multi-channel` | false | Channels summed for display |
| `equalizer-nbands` | `num-bands` | 10 | Not `equalizer-10bands`, whose centres are fixed and cannot be Winamp's — see BUG-004 |
| `equalizer-nbands` | band `freq` / `bandwidth` / `gain` | per §Equaliser | Set on the ten child bands through `GstChildProxy` |
| `volume` (preamp) | `volume` | 10^((preamp − attenuation)/20) | Carries both the preamp and the headroom attenuation |
| `audiomixmatrix` | `in-channels` / `out-channels` | 2 / 2 | Stereo, pinned by the preceding capsfilter |
| `audiomixmatrix` | `matrix` | diagonal | Per-channel gain from §Volume taper; must be set before linking |

The distinction between analysis bands (512) and display bands is deliberate. Requesting the display band count directly from the element collapses the lowest two octaves into a single bar, because the element's bands are linearly spaced. See AV-011.

---

## Equaliser

Ten bands, matching the classic Winamp layout per D-007.

| Band | Index | Centre frequency | Range |
|------|-------|------------------|-------|
| 1 | 0 | 60 Hz | −12 to +12 dB |
| 2 | 1 | 170 Hz | −12 to +12 dB |
| 3 | 2 | 310 Hz | −12 to +12 dB |
| 4 | 3 | 600 Hz | −12 to +12 dB |
| 5 | 4 | 1 kHz | −12 to +12 dB |
| 6 | 5 | 3 kHz | −12 to +12 dB |
| 7 | 6 | 6 kHz | −12 to +12 dB |
| 8 | 7 | 12 kHz | −12 to +12 dB |
| 9 | 8 | 14 kHz | −12 to +12 dB |
| 10 | 9 | 16 kHz | −12 to +12 dB |

**Bandwidths.** Each band's bandwidth is the distance between the geometric
midpoints to its neighbours, mirrored at the two ends, so the bands are
contiguous without overlapping at their −3 dB points. The Winamp centres are not
octave-spaced and crowd at the top, so the upper three bands are necessarily
narrow and high-Q; that is a property of the layout D-007 inherits deliberately,
not of the implementation.

| Band | Centre | Bandwidth | Q |
|------|--------|-----------|---|
| 1 | 60 Hz | 65.3 Hz | 0.92 |
| 2 | 170 Hz | 128.6 Hz | 1.32 |
| 3 | 310 Hz | 201.7 Hz | 1.54 |
| 4 | 600 Hz | 343.3 Hz | 1.75 |
| 5 | 1 kHz | 957.5 Hz | 1.04 |
| 6 | 3 kHz | 2510.6 Hz | 1.19 |
| 7 | 6 kHz | 4242.6 Hz | 1.41 |
| 8 | 12 kHz | 4476.2 Hz | 2.68 |
| 9 | 14 kHz | 2005.1 Hz | 6.98 |
| 10 | 16 kHz | 2138.1 Hz | 7.48 |

**Preamp:** −12 to +12 dB, applied ahead of the band filters.

**Gain ramp time:** 30 ms linear interpolation on any gain change, to prevent zipper noise during slider drags. **Provisional.**

The interpolation is driven by the application rather than by a GStreamer control source. `equalizer-nbands` advertises its band gains as controllable and accepts a control binding without error, but never calls `gst_object_sync_values` on its bands while streaming, so a bound source is silently inert — see BUG-006. `Equaliser` therefore steps the property from a 5 ms timer and computes the fraction from a clock rather than a tick count, so timer jitter cannot stretch or shorten the ramp. The element re-reads the gain once per buffer regardless, which is the real granularity. A change that changes nothing starts no ramp.

**Headroom.** The combined gain path can exceed full scale, and nothing prevents
it. The engine reports how far, and does not attenuate.

```
excess_dB = max(0, preamp_dB + cascade_peak_dB)
```

where `cascade_peak_dB` is `20·log₁₀(max|H(f)|)` for the whole band cascade at
the current curve, evaluated over log-spaced frequencies from 20 Hz to 20 kHz.

**It is not the largest single band gain.** Ten peaking sections in series
multiply where their skirts overlap, and the Winamp centres overlap heavily: all
ten bands at +12 dB peak at **+21.4 dB near 607 Hz**, not +12 dB. See BUG-005.

The response is modelled at 192 kHz. It grows slightly with sample rate (21.4 dB
at 44.1 kHz against 23.7 dB at 192 kHz for the worst curve), so modelling the
highest supported rate means the figure bounds the real one at every lower rate
and no knowledge of the negotiated rate is needed. Measured against a real
capture: 32.04 dB actual against 35.69 dB reported.

**Why nothing is attenuated.** An earlier design subtracted exactly this figure
before the sink, so that output could never exceed full scale. Because the
figure *is* the largest gain the curve produces, subtracting it cancelled every
boost: raising a band lowered everything else instead of lifting that band,
raising the preamp did nothing whatsoever, and only cuts worked. The equaliser
was a cut-only device. See BUG-008.

Level is the preamp's job, as it is in Winamp, foobar2000 and VLC. AV-003's
concern was that clipping inside an IIR cascade could drive the filters
unstable; since BUG-007 the chain runs in `F32LE`, where levels above unity are
ordinary and no instability follows. What remains is clipping at the sink
conversion — audible distortion under settings the user chose, and visible to
them through the reported figure.

**Bypass.** When the equaliser is off, the element is set to unity on all bands and zero preamp rather than being removed from the pipeline, so that bypass does not cause a graph rebuild. Output must be bit-identical to the unprocessed signal in this state; this is the acceptance criterion for F-020.

**`.eqf` import.** Winamp preset files store eleven bytes per preset — ten band values and a preamp — each in the range 0 to 63, with 31 representing 0 dB. Mapping to decibels is `dB = (31 − value) × 12 / 31`. Note the inversion: lower stored values are higher gain. RS-1 maps gains only; the underlying filter response is not guaranteed to match Winamp's, per D-006.

---

## Meters

### Data contract

`MeterTexture` exposes an `N×1` two-channel 16-bit texture, where `N` is the display band count.

| Channel | Carries | Range |
|---------|---------|-------|
| R | Current band magnitude, normalised | 0.0 to 1.0 |
| G | Peak-hold cap for that band, normalised | 0.0 to 1.0 |

Normalisation maps the `spectrum` threshold (−80 dB) to 0.0 and 0 dB to 1.0. Sampling uses linear filtering; shaders may sample between texel centres to interpolate between bands.

**Display band count:** 24 for spectrum bars, 48 for mirrored spectrum. **Provisional** — chosen for legibility at typical panel widths, to be confirmed during Phase 4.

### Frequency mapping

Analysis bins are bucketed into display bands logarithmically across 20 Hz to 20 kHz:

```
f_lower(i) = 20 × (20000/20)^(i/N)
f_upper(i) = 20 × (20000/20)^((i+1)/N)
```

Each display band takes the maximum of the analysis bins whose centre frequencies fall within its range. Maximum rather than mean, because averaging across a wide upper band buries transients that are the visually interesting part.

Where a display band spans fewer than one analysis bin — which happens at the bottom of the range — the nearest bin is used and the band is marked as interpolated. No display band may be left empty. See AV-011.

Measured at 44.1 kHz with 512 analysis bins, whose bins are 43.07 Hz wide: **4 of the 24 display bands and 12 of the 48** fall below one bin and are interpolated. The consequence is visible and is accepted rather than hidden — an interpolated band borrows the nearest bin, which already belongs to a real band, so the lowest few bars move in lockstep. At 24 bands, band 3 shares bin 1 with band 4 and band 6 shares bin 3 with band 7. Bars that move together are honest about the analysis resolution; a bar stuck at silence would not be, and would read as missing bass rather than as a limit of the transform.

### Smoothing and ballistics

**Spectrum smoothing.** Exponential moving average per band, asymmetric: attack coefficient 0.6, release coefficient 0.15, applied per 16 ms update. Fast rise, slow fall, which is what makes a spectrum display readable rather than frantic. **Provisional.**

**Peak-hold.** Cap rises instantly to any new maximum, holds for 1500 ms, then falls at 20 dB/s.

**VU ballistics.** The needle is a **second-order** system reaching 99% of full deflection at 300 ms for a steady sine at reference level, with 1% to 1.5% overshoot, matching the IEC 60268-17 standard VU characteristic.

Second-order, not first-order. A first-order step response is monotonic — it approaches its final value and never passes it — so it cannot overshoot by any amount, and the overshoot is where a real needle's visible settle-back comes from. This section previously specified a first-order system *and* an overshoot, which are two different systems; see BUG-010.

Solved for the stated behaviour, first reaching 99% at exactly 300 ms:

| Overshoot | Damping ratio ζ | ωn (rad/s) | Peak at |
|-----------|-----------------|------------|---------|
| 1.00% | 0.8261 | 13.973 | 399 ms |
| 1.25% | 0.8127 | 13.512 | 399 ms |
| 1.50% | 0.8007 | 13.126 | 400 ms |

The 1.25% midpoint is used: ζ = 0.8127, ωn = 13.512 rad/s. **Provisional** — the point chosen within the standard's range is open, though the order of the system is not. Measured against the implementation: 99% at 302.0 ms, 1.16% overshoot peaking at 401 ms. This is the single most important constant in the meter design: instantaneous RMS looks twitchy and immediately reads as a fake, and the lag is the entire character of the instrument.

Reference level (0 VU) maps to −18 dBFS. **Provisional** — this is a broadcast convention rather than a universal one, and may want to be user-configurable.

**Peak indicator.** Rendered alongside the VU needle, driven directly from `level`'s peak values with the element's own TTL and falloff. No additional smoothing.

### Display modes

| Mode | Identifier | Source channels | Notes |
|------|-----------|-----------------|-------|
| Spectrum bars | `spectrum` | Summed | Default |
| Mirrored spectrum | `spectrum-mirror` | Summed | Reflected about the horizontal axis |
| Stereo VU | `vu` | Per channel | Two needles |
| LED peak ladder | `ladder` | Per channel | Segmented, no interpolation between segments |
| Oscilloscope | `scope` | Per channel | Candidate, F-034; needs the PCM tap |

Mode is cycled by clicking the display and persisted under the settings key below.

---

## Metadata

Tags are read on a private worker pool, never on the main thread, so that adding
a large folder cannot block the interface (F-010, AV-008). A row exists and is
displayable from the moment it is added, carrying a title derived from the file's
base name until real tags arrive.

A file that does not resolve is marked `Missing`; one that resolves but cannot be
parsed as audio is marked `Failed`. Neither is an error condition — both are
ordinary states a row can hold, and both must render rather than disappear. See
AV-009 and AV-010.

### Duration

Two sources report duration and they do not always agree.

The **tag** is what the playlist shows before a track has been played. For most
formats it is exact. For a VBR MP3 it is exact only when the file carries a Xing
or VBRI header. Without one the figure is extrapolated from the first frame's
bitrate and can be badly wrong: measured on a headerless VBR fixture, TagLib
reported 38 872 ms against an actual 32 486 ms, an overestimate of 20%. TagLib's
`Fast`, `Average` and `Accurate` accuracy modes all return that same figure, so
raising the accuracy setting is not a remedy — it only buys a full-file scan for
no gain. With a Xing header present the same encoder's output reads 32 575 ms
against a true 32 575 ms.

The **engine** demuxes the stream and is authoritative. When a track reaches
`PLAYING` and the pipeline answers a duration query, that value replaces the
tag-derived one for that row through `PlaylistModel::setAuthoritativeDuration`.

A duration of `-1` means unknown and is rendered as such, never as zero. A
declared duration of zero is treated as absent, because zero is what a malformed
tag reports and believing it makes seeking wrong.

---

## Playlist file formats

**M3U / M3U8.** Extended form with `#EXTINF:` duration and title lines. M3U8 is UTF-8; plain M3U is written as UTF-8 with a note that this diverges from the original Latin-1 convention, because every other modern player does the same. Paths are written relative when the playlist file is at or above the media in the tree, absolute otherwise.

**PLS.** Read and write. `NumberOfEntries` and `Version=2` written; `Length=-1` for unknown durations.

Both formats are read tolerantly — unknown directives are ignored rather than treated as errors — and written strictly.

---

## Settings

`QSettings`, INI format, at `$XDG_CONFIG_HOME/ferrolux/ferrolux.ini`.

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `playback/volume` | double | 0.7 | Taper position, not amplitude |
| `playback/balance` | double | 0.0 | −1.0 left, +1.0 right |
| `playback/repeat` | string | `off` | `off` / `all` / `one` |
| `playback/shuffle` | bool | false | |
| `equaliser/enabled` | bool | false | |
| `equaliser/preamp` | double | 0.0 | dB |
| `equaliser/bands` | list\<double\> | ten zeroes | dB, band order as above |
| `equaliser/preset` | string | `flat` | Name only; values are authoritative |
| `equaliser/user/<name>` | list\<double\> | — | One key per user preset, the name being the key. Eleven values: ten band gains in band order, then the preamp. A name containing `/` is rejected, since it would open a settings subgroup rather than name a preset. A user preset shadows a built-in of the same name. |
| `meters/mode` | string | `spectrum` | Identifier from the display mode table |
| `meters/reference-level` | double | −18.0 | dBFS for 0 VU |
| `ui/theme` | string | `ferric` | Token set name |
| `ui/compact` | bool | false | F-042 |
| `ui/geometry` | bytearray | — | Window position and size |
| `session/playlist` | string | — | Path to the auto-saved session playlist |
| `session/position` | int64 | 0 | Nanoseconds into the current track |

Keys are append-only in the same sense as document IDs: a key that changes meaning gets a new name and a migration step, rather than being silently reinterpreted.

---

## Volume taper

Displayed volume `v` in the range 0.0 to 1.0 maps to linear amplitude as `a = v³`. Cubic rather than linear, because linear amplitude control feels like it does nothing across the top half of the range and everything across the bottom tenth.

Balance `b` in the range −1.0 to +1.0 attenuates the channel being turned away from, and never boosts either channel:

```
left  = min(1, 1 − b)
right = min(1, 1 + b)
```

Attenuate-only rather than constant-power. A `cos`/`sin` constant-power law is the correct law for panning a **mono** source into a stereo field, where the identity `L² + R² = 1` describes one source's total power. Ferrolux scales the two channels of an already-stereo signal independently, and that identity says nothing about the result: applied here, the constant-power law attenuates centred playback by 3.01 dB on both channels and makes a channel 3 dB *louder* when the control is moved off centre — the opposite of what a balance control should do, and the opposite of what this section previously claimed as its rationale. See BUG-003.

Under the law above, centre is unity on both channels and neither gain can exceed unity, so balance is not a contributor to clipping under AV-003.

---

## Design tokens

Panel appearance is defined by a named token set, resolved at load. The `ferric` set is the default and is the appearance the project is named for.

| Token | Value | Role |
|-------|-------|------|
| `shell` | `#B4B2A9` | Chassis face |
| `shell-recess` | `#D3D1C7` | Raised control surfaces |
| `shell-edge` | `#888780` | Bevel and division lines |
| `display-bg` | `#2C2C2A` | Readout background |
| `readout` | `#EF9F27` | Primary readout amber |
| `readout-dim` | `#BA7517` | Secondary readout amber |
| `readout-floor` | `#854F0B` | Lowest active segment, and the unlit-segment ghost layer |
| `ink` | `#2C2C2A` | Legends on the shell |

Bevel geometry, corner radii and the type scale are specified alongside the palette in the token file rather than here, because they are authored as data. The faces themselves are not data — they are fixed for the project and are specified below. Components reference tokens by name and never contain literal colour values; this is what makes F-044 a token swap rather than an asset pack.

### Typography

Four faces, one per type role. All four are under the SIL Open Font License 1.1, which permits bundling and redistribution under any project licence settled for D-010, including in a Flatpak and a `.deb`. They are bundled in `resources/fonts/` together with their licence texts.

| Token | Face | Role |
|-------|------|------|
| `type-readout-numeric` | DSEG7 Classic | Elapsed and remaining time, and every other numeric field inside a readout |
| `type-readout-text` | Handjet | Track title, artist and playlist rows — the dot-matrix readout |
| `type-readout-segment` | DSEG14 Classic | Segmented starburst alternative to `type-readout-text`, for a VFD panel variant under F-044 |
| `type-legend` | IBM Plex Sans Condensed | Silkscreened chassis text: section headings, equaliser band labels, control legends |

**Lit versus printed.** The division between these roles is not only
typographic. `type-readout-*` faces in the `readout` palette are for values the
instrument reports and that change — time, volume, balance position, band gains.
`type-legend` in `ink` is for text printed on the chassis, which names a control
and never changes. Rendering a value as a legend makes it look inert; rendering
a legend as a readout implies a state it does not have. See F-040.

No face outside this table appears on the control surface. A role that needs a face it does not have is a specification gap to be recorded here, not a licence to fall back to a system font.

**Role separation is a hard rule.** `type-readout-numeric` renders digits and separators only. A seven-segment alphabet cannot distinguish 5 from S, 6 from b, or 0 from O, so DSEG7 must never be given text. Track titles carry arbitrary Unicode, which is why `type-readout-text` is Handjet: it covers Latin, Cyrillic, Greek, Armenian, Hebrew, Arabic and Korean, so a non-Latin title degrades to a wider glyph set from the same face rather than to a substituted system face in the middle of a lit readout.

**Handjet axis settings.** Handjet is a variable font, but Ferrolux ships a **static instance** of it, generated at the values below, rather than the variable file.

Setting a variable axis at runtime requires `QFont::setVariableAxis`, introduced in Qt 6.7. Neither Ubuntu 24.04 nor the Linux Mint 22 series derived from it can provide that, and on an older Qt the variable file renders its default square element instead of the specified circle — silently, with no error. Instancing removes the requirement entirely. See BUG-001.

| Axis | Range | Instanced at | Effect |
|------|-------|--------------|--------|
| `ELSH` | 0.0–16.0 | 8.0 | Element shape. 2.0 is a square, 8.0 a circle. **Provisional.** |
| `ELGR` | 1.0–2.0 | 1.0 | Elements per grid unit. 1.0 is one element per cell; 2.0 is a 2×2 group. **Provisional.** |
| `wght` | 100–900 | 500 | Element size, and therefore the gap between adjacent dots. **Provisional.** |

Weight here controls spacing, not boldness. A physical dot-matrix cell shows discrete separated dots, so the value is chosen for a visible gap between neighbours rather than for stroke weight.

The instance is produced with `fonttools varLib.instancer` and, per the OFL Reserved Font Name clause, is renamed rather than shipped under the Handjet name. The consequence is that changing the dot-matrix character means regenerating the instance rather than editing a token value: this is the one place in the token set where F-044's token-swap property does not hold, and it is a deliberate trade against a toolchain requirement the target distributions cannot meet.

**Unlit segments.** Physical LED and VFD readouts show their inactive segments faintly rather than not at all, and omitting this is the most common tell of a simulated readout. It is not a font feature. Each readout renders two text layers at identical face, size and position: a ghost layer of the all-segments-lit string in `readout-floor`, with the live string in `readout` composited over it. This costs one extra text node per readout and nothing else.

The ghost string for `type-readout-numeric` is `8` repeated across the field width, with the separators lit — `88:88` for a time display. The equivalent for `type-readout-text` is a full-cell glyph repeated across the field, and the specific codepoint Handjet uses for it is **provisional** pending Phase 5. Where a face has no all-lit glyph, the ghost layer is omitted for that readout rather than approximated.

**Scaling.** All four faces are geometric outlines rather than pixel-grid designs, and stay correct at fractional device pixel ratios. Faces drawn on an integer pixel grid — the pixel and bitmap-revival category generally, however well it suits the period — are excluded from the control surface, because at 1.5× and 2.5× they soften in exactly the way AV-005 exists to prevent.
