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
    → equalizer-10bands
    → level
    → spectrum
    → audioconvert
  )
  audio-sink = autoaudiosink
```

`level` and `spectrum` are pass-through analysis elements and do not alter the signal. Both sit after the equaliser so that the meters display the signal as heard rather than as decoded.

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

**Preamp:** −12 to +12 dB, applied ahead of the band filters.

**Gain ramp time:** 30 ms linear interpolation on any gain change, to prevent zipper noise during slider drags. **Provisional.**

**Headroom rule.** The sum of preamp and the maximum band gain may exceed available headroom. The engine applies an automatic attenuation of `max(0, preamp_dB + max_band_dB − 0 dBFS_margin)` where `0 dBFS_margin` is 0 dB, before the sink. This attenuation is not user-visible and is not part of the displayed gain values. See AV-003.

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

### Smoothing and ballistics

**Spectrum smoothing.** Exponential moving average per band, asymmetric: attack coefficient 0.6, release coefficient 0.15, applied per 16 ms update. Fast rise, slow fall, which is what makes a spectrum display readable rather than frantic. **Provisional.**

**Peak-hold.** Cap rises instantly to any new maximum, holds for 1500 ms, then falls at 20 dB/s.

**VU ballistics.** The needle is a first-order system with a 300 ms integration time to 99% of full deflection for a steady sine at reference level, matching the IEC 60268-17 standard VU characteristic. Overshoot is 1% to 1.5%. This is the single most important constant in the meter design: instantaneous RMS looks twitchy and immediately reads as a fake, and the lag is the entire character of the instrument.

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

Balance `b` in the range −1.0 to +1.0 maps to constant-power pan:

```
left  = cos((b + 1) × π/4)
right = sin((b + 1) × π/4)
```

Constant power rather than linear, so that a centred image does not lose perceived loudness relative to a hard-panned one.

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

No face outside this table appears on the control surface. A role that needs a face it does not have is a specification gap to be recorded here, not a licence to fall back to a system font.

**Role separation is a hard rule.** `type-readout-numeric` renders digits and separators only. A seven-segment alphabet cannot distinguish 5 from S, 6 from b, or 0 from O, so DSEG7 must never be given text. Track titles carry arbitrary Unicode, which is why `type-readout-text` is Handjet: it covers Latin, Cyrillic, Greek, Armenian, Hebrew, Arabic and Korean, so a non-Latin title degrades to a wider glyph set from the same face rather than to a substituted system face in the middle of a lit readout.

**Handjet axis settings.** Handjet is a variable font whose element shape and density are axes rather than separate files, so the dot-matrix character is a token value rather than a choice of asset.

| Axis | Range | `ferric` value | Effect |
|------|-------|----------------|--------|
| `ELSH` | 0.0–16.0 | 8.0 | Element shape. 2.0 is a square, 8.0 a circle. **Provisional.** |
| `ELGR` | 1.0–2.0 | 1.0 | Elements per grid unit. 1.0 is one element per cell; 2.0 is a 2×2 group. **Provisional.** |
| `wght` | 100–900 | 500 | Element size, and therefore the gap between adjacent dots. **Provisional.** |

Weight here controls spacing, not boldness. A physical dot-matrix cell shows discrete separated dots, so the value is chosen for a visible gap between neighbours rather than for stroke weight.

**Unlit segments.** Physical LED and VFD readouts show their inactive segments faintly rather than not at all, and omitting this is the most common tell of a simulated readout. It is not a font feature. Each readout renders two text layers at identical face, size and position: a ghost layer of the all-segments-lit string in `readout-floor`, with the live string in `readout` composited over it. This costs one extra text node per readout and nothing else.

The ghost string for `type-readout-numeric` is `8` repeated across the field width, with the separators lit — `88:88` for a time display. The equivalent for `type-readout-text` is a full-cell glyph repeated across the field, and the specific codepoint Handjet uses for it is **provisional** pending Phase 5. Where a face has no all-lit glyph, the ghost layer is omitted for that readout rather than approximated.

**Scaling.** All four faces are geometric outlines rather than pixel-grid designs, and stay correct at fractional device pixel ratios. Faces drawn on an integer pixel grid — the pixel and bitmap-revival category generally, however well it suits the period — are excluded from the control surface, because at 1.5× and 2.5× they soften in exactly the way AV-005 exists to prevent.
