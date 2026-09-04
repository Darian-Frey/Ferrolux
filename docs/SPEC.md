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

`MeterTexture` exposes an `N×1` texture, where `N` is the display band count.

| Channel | Carries | Resolution |
|---------|---------|------------|
| R, G | Current band magnitude, normalised 0.0 to 1.0 | 16 bits, high byte then low |
| B | Peak-hold cap for that band, normalised 0.0 to 1.0 | 8 bits |
| A | Unused, held at 255 | — |

This layout packs a 16-bit magnitude across two 8-bit channels rather than using
a genuine two-channel 16-bit format. `QQuickWindow::createTextureFromImage`
produces an 8-bit-per-channel texture whatever image format it is given, and
reaching past it to the RHI for an `RG16` target is fragile across backends for
a texture of at most 48 texels. The packing delivers the sixteen bits this
section actually requires, on a format every backend supports. Measured
round-trip error is 7.63 × 10⁻⁶, which is 514 times finer than a single 8-bit
channel would allow.

Eight bits is left for the peak cap deliberately. Magnitude drives bar height
and is what a viewer watches decay, where 8-bit steps of 1/255 show as
stair-stepping on a large display; the cap is a two-pixel line whose position
8 bits resolves past the point of noticing.

**Alpha is held at 255 and carries nothing.** This is a hard constraint, not a
spare channel awaiting a use. The scene graph normalises an image that has an
alpha channel to a premultiplied format when it uploads it, scaling R, G and B by
A — and R and G are the magnitude. Putting a value there corrupts every mode at
once, silently and proportionally, so the display still looks plausible. It has
been tried: 270,221 of 518,400 pixels of the spectrum display changed. Data that
a shader needs per frame and cannot pack into R, G or B travels as a uniform.
See BUG-016.

A shader reconstructs the magnitude as `(R × 256 + G) / 255` in normalised
sampler units, or equivalently `dot(texel.rg, vec2(65280.0, 255.0)) / 65535.0`
when reading bytes.

Normalisation maps the `spectrum` threshold (−80 dB) to 0.0 and 0 dB to 1.0. Sampling uses linear filtering; shaders may sample between texel centres to interpolate between bands.

**Display band count:** 24 for spectrum bars, 48 for mirrored spectrum and for flame. **Provisional** — chosen for legibility at typical panel widths, to be confirmed during Phase 4.

### Frequency mapping

Analysis bins are bucketed into display bands logarithmically across 20 Hz to 20 kHz:

```
f_lower(i) = 20 × (20000/20)^(i/N)
f_upper(i) = 20 × (20000/20)^((i+1)/N)
```

Each display band takes the maximum of the analysis bins whose centre frequencies fall within its range. Maximum rather than mean, because averaging across a wide upper band buries transients that are the visually interesting part.

Where a display band spans fewer than one analysis bin — which happens at the bottom of the range — the nearest bin is used and the band is marked as interpolated. No display band may be left empty. See AV-011.

Measured at 44.1 kHz with 512 analysis bins, whose bins are 43.07 Hz wide: **4 of the 24 display bands and 12 of the 48** fall below one bin.

Such a band's centre is expressed in fractional bin coordinates and its magnitude is **interpolated in decibels between the two bins either side**. It is not rounded to the nearest bin. Rounding gives adjacent starved bands one identical value, and the result is visible: at 24 bands the three lowest bars move as a single welded group and the next two as another. Interpolating gives each band its own value, so the bottom of the display moves as a spectrum rather than as a set of linked levers.

The resolution limit is still real and is not hidden by this — neighbouring low bands remain highly correlated because they genuinely describe overlapping content. What changes is that they are no longer identical, which is the difference between a display that is honest about its resolution and one that looks broken.

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

Reference level (0 VU) maps to **−9 dBFS**, and is configurable under
`meters/reference-level`.

Not −18 dBFS. That is EBU broadcast alignment, which assumes programme far
quieter than a consumer master. Measured across six ordinary tracks: sustained
RMS runs −10 to −22 dBFS, mostly near −15, with loud passages reaching −6.3.
Against −18 that pegs the needle before the music starts. Against −9 the same
material settles near half deflection and its loudest moments land at 1.36, the
top of the needle's travel — which is the span the instrument exists to show.
See BUG-014.

The needle travels to 1.4, not to 1.0. A VU meter's scale continues past 0 VU
and the arc changes colour there, so a needle above reference reads as *over*
rather than as one resting against its end stop.

Deflection is linear in amplitude rather than in decibels, which is why a VU
scale crowds towards its left end as a real one does.

**Peak indication is a different scale and must not share the VU's.** A sample
peak legitimately runs ten decibels or more above RMS, so any mapping that suits
one pegs the other. Peaks use a decibel scale ending at full scale with a floor
of −60 dB, as hardware peak meters do: −10.6 dBFS reads 0.823, −1 dBFS reads
0.983. The LED ladder's over-level threshold sits at −6 dBFS so that its hot
colour means near clipping rather than merely loud.

**Peak indicator.** Rendered alongside the VU needle, driven directly from `level`'s peak values with the element's own TTL and falloff. No additional smoothing.

On the peak scale above, so the lamp lights from about −4 dBFS. A lamp that lights on ordinary programme conveys nothing; it exists to say the signal is close to full scale, which is precisely what the needle's ballistic cannot show.

### Rest and hold

Stopping and pausing are different events, and the display distinguishes them.

**Pause holds.** The needles and bars stay where the music left them. The signal
has not gone away; it is suspended, and a deck that dropped its meters on pause
would be lying about that.

**Stop releases to rest.** The bars fall at the spectrum's own release
coefficient and the needles fall under their own ballistic, because that fall is
the same physical system as the rise. A needle that snaps to zero reads as a
meter being switched off rather than one that has stopped being driven.
Anything still held in the scheduling queue is discarded, since it describes
audio that will now never be heard.

The engine's state is the authority, so this holds however playback came to a
halt — the end of a playlist, a file that failed, or the transport button.

### Display modes

| Mode | Identifier | Source channels | Notes |
|------|-----------|-----------------|-------|
| Spectrum bars | `spectrum` | Summed | Default |
| Mirrored spectrum | `spectrum-mirror` | Summed | Reflected about the horizontal axis |
| Flame | `flame` | Summed | F-035. Continuous curve rather than bars, shaded in contour steps. Uses the finer band count for the same reason the mirrored mode does |
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
| `meters/reference-level` | double | −9.0 | dBFS for 0 VU. Not the −18 broadcast figure — see §Meters and BUG-014 |
| `ui/theme` | string | `ferric` | Token set name. A name that no longer resolves falls back to the default and says so — a set renamed or removed since it was chosen is a stale setting, not a reason to start with no appearance |
| `ui/display-inverted` | bool | false | Draws the display well lit-ground with dark text rather than dark-ground with lit text. A different instrument rather than the same one recoloured, so it is a setting and not a token |
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
| `display-bg` | `#2C2C2A` | Readout background — see the rule below |
| `readout` | `#EF9F27` | Primary readout amber |
| `readout-dim` | `#BA7517` | Secondary readout amber |
| `readout-floor` | `#854F0B` | Lowest active segment, and the unlit-segment ghost layer |
| `ink` | `#2C2C2A` | Legends on the shell |

**`readout-dim` is not a body-text colour.** At 3.76:1 against `display-bg` it is below the 4.5:1 that normal text needs, and the readout faces are dot-matrix and segmented rather than plain, which asks more of the contrast rather than less. It is for annotation beside something else — a transport state under a title, a mode name, a secondary bar in a display — and never for text a reader is working through. Where one item among many has to be distinguished, light the *ground* behind it rather than dimming its neighbours: a backlit row is unmistakable and leaves everything else readable, where brightness alone gives 1.7× and costs the rest their legibility. See BUG-020.

**`display-bg` is a requirement, not a label.** A readout is lit text over an unlit ghost, and that arrangement only works over a ground darker than both layers. Put one on the chassis instead and the relationship inverts: `readout-floor` is a dark brown with *more* contrast against the light shell than the amber value drawn over it, so the segments that are off read louder than the number that is on. The field is then drawn exactly to specification and is hard to read because of it — which is how it reached a screenshot before anyone noticed. Every readout therefore sits in a well or carries its own window, as the numerals on a deck do.

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

**The transport marks are drawn, not set.** ▶ ❚❚ ■ ⏮ ⏭ are legends by the rule above — they name a control and never change — but the legend face carries none of them: IBM Plex Sans Condensed has no U+25B6, U+25C0, U+25A0, U+23F8 or U+23EE. An icon font would be a fifth face on the control surface, which this section forbids, and a bitmap would be the defect the project exists to avoid. So they are drawn as half-plane intersections in `qml/shaders/transport.frag`, antialiased from the screen-space derivative rather than at a fixed edge width, which is exactly the answer D-003 already gives: every element of the control surface is vector geometry or a fragment shader.

**Role separation is a hard rule.** `type-readout-numeric` renders digits and separators only.

That is a statement about the file and not only about taste. DSEG7 Classic carries the digits, the full stop, the colon and a **minus**, and carries no plus sign and no per-cent sign. So a signed value in a readout shows its sign only when negative — which is what a deck's gain display does anyway — and a unit is silkscreened beside the field in `type-legend` rather than set in it. Reaching for a `+` or a `%` does not fail: Qt substitutes a system face for that one character, in the middle of a lit field, which is exactly the failure this rule exists to prevent. A seven-segment alphabet cannot distinguish 5 from S, 6 from b, or 0 from O, so DSEG7 must never be given text. Track titles carry arbitrary Unicode, which is why `type-readout-text` is Handjet: it covers Latin, Cyrillic, Greek, Armenian, Hebrew, Arabic and Korean, so a non-Latin title degrades to a wider glyph set from the same face rather than to a substituted system face in the middle of a lit readout.

**Handjet axis settings.** Handjet is a variable font, but Ferrolux ships a **static instance** of it, generated at the values below, rather than the variable file.

Setting a variable axis at runtime requires `QFont::setVariableAxis`, introduced in Qt 6.7. Neither Ubuntu 24.04 nor the Linux Mint 22 series derived from it can provide that, and on an older Qt the variable file renders its default square element instead of the specified circle — silently, with no error. Instancing removes the requirement entirely. See BUG-001.

| Axis | Range | Instanced at | Effect |
|------|-------|--------------|--------|
| `ELSH` | 0.0–16.0 | 8.0 | Element shape. 2.0 is a square, 8.0 a circle. Confirmed 2026-09-03. |
| `ELGR` | 1.0–2.0 | 1.0 | Elements per grid unit. 1.0 is one element per cell; 2.0 is a 2×2 group. Confirmed 2026-09-03. |
| `wght` | 100–900 | **400** | Element size, and therefore the gap between adjacent dots. Was 500, then 300; see below. |

Weight here controls spacing, not boldness. A physical dot-matrix cell shows discrete separated dots, so the value is chosen for a visible gap between neighbours rather than for stroke weight.

**500 did not meet that intent, and 300 overcorrected.** Rendered across the axis, the elements at 500 touch at every size the panel uses, and the face reads as continuous strokes — a condensed sans with notches, not a dot-matrix. Separation becomes visible at 300 and clearer below it, at the cost of the readout going faint. Recorded as BUG-017.

300 then proved too light to read in a list. The separation it buys only exists above the minimum size below, and a playlist row is under it — so the thinness bought nothing there and cost stroke weight where the reading actually happens. 400 separates at the sizes where separation is possible and is legibly heavier at list size. Recorded as BUG-020.

Two consequences follow, and neither is optional:

- **The dot-matrix readout has a minimum size**, and it binds everywhere the face is used. `type-readout-text` is assigned to the track title *and to playlist rows*, so both are subject to it. Below roughly 20 device-independent units the elements merge whatever the weight, and below 14 the face becomes ambiguous rather than merely soft — at 12, `Reebok` reads as `Aeebok`, which is worse than a plain face would be. `size-readout-large` is 20 and `size-readout` is 16: 16 is where a capital R stops being mistakeable, and a playlist row cannot afford 20 without showing a third fewer entries. A dot-matrix face too small to show its dots is a costly way to obtain a plain one.
- **The instance is named for its axis values**, `Handjet Light Circle Single`, which is what `type-readout-text` asks for. Changing the weight changes the family name, so the token set and the generator move together.

The instance is produced with `fonttools varLib.instancer` by `tools/make-fonts.sh`, which also fetches the other three faces so that what is committed under `resources/fonts/` has a stated provenance rather than being binaries of unclear origin.

**Handjet declares no Reserved Font Name**, so nothing in the licence requires the instance to be renamed. An earlier revision of this document said otherwise and cited a clause that does not apply — the only occurrence of the phrase in Handjet's licence file is the OFL's own boilerplate definition, not a reservation on the copyright line. See BUG-018. The instance is nonetheless not called plain `Handjet`: `fonttools` derives a family name from the pinned axis values, and the file is not Handjet as published, so naming it as though it were would misreport what has been shipped. DSEG does reserve its name and is redistributed unmodified. The consequence is that changing the dot-matrix character means regenerating the instance rather than editing a token value: this is the one place in the token set where F-044's token-swap property does not hold, and it is a deliberate trade against a toolchain requirement the target distributions cannot meet.

**Unlit segments.** Physical LED and VFD readouts show their inactive segments faintly rather than not at all, and omitting this is the most common tell of a simulated readout. It is not a font feature. Each readout renders two text layers at identical face, size and position: a ghost layer of the all-segments-lit string in `readout-floor`, with the live string in `readout` composited over it. This costs one extra text node per readout and nothing else.

The ghost string for `type-readout-numeric` is `8` repeated across the field width, with the separators lit — `88:88` for a time display, `-88` for a gain that spans −12 to 12.

**The live string is aligned into the ghost's cells, not centred in it.** A segmented display has fixed cells and lights some of them; a one-character value centred over a three-character ghost sits *between* two unlit cells, which no physical readout does. Numeric fields are therefore right-aligned against a ghost of the field's full width, so a short value lights the low-order cells and leaves the leading ones dark. Where a face has no all-lit glyph, the ghost layer is omitted for that readout rather than approximated.

**`type-readout-text` has no ghost**, which resolves the codepoint left provisional here pending Phase 5: the bundled instance has none. Of its 1,339 glyphs it carries no U+2588 FULL BLOCK, U+25A0 BLACK SQUARE, U+2593 DARK SHADE or U+25CF BLACK CIRCLE, and nothing else in it fills a cell. A row of some other dense character would be a different string in the same face rather than the same string unlit, which is the approximation the rule above already forbids. The time readout keeps its ghost; the title does not, and the asymmetry is a property of the faces rather than an inconsistency in the panel.

**Scaling.** All four faces are geometric outlines rather than pixel-grid designs, and stay correct at fractional device pixel ratios. Faces drawn on an integer pixel grid — the pixel and bitmap-revival category generally, however well it suits the period — are excluded from the control surface, because at 1.5× and 2.5× they soften in exactly the way AV-005 exists to prevent.
