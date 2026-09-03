# Ferrolux

**A cassette futurism audio player for Linux.**

Transport, playlist, a ten-band equaliser and switchable VU and spectrum
displays — the scope Winamp had — rendered as resolution-independent vector
chrome rather than bitmap skins, so the panel is correct at any display scale.

> **Status: Phases 1 to 4 built, Phase 5 under way.** Transport, a 20,000-entry
> playlist, shuffle, repeat, playlist file I/O, gapless playback, a ten-band
> equaliser and five shader-rendered meter displays all work, behind a
> deliberately plain harness. Every display holds 60 fps at 3840×2160 with at
> least 46% of the frame budget spare — measured, not assumed. There is no panel
> yet — that is Phase 5, and it is the reason the project exists. Its token set
> and its first lit readouts are in. See [Status](#status) below.

---

## Why this exists

Ferrolux began as an intention to fork [qmmp](https://qmmp.ylsoftware.com/) and
fix its classic-skin scaling. Working out what "fixing" would actually mean
established that the defect is not in any skin engine's implementation.

Classic Winamp skins are fixed-size bitmaps authored for a 275×116 window. Any
scaling is resampling, and resampling bitmap chrome either blurs it or produces
nearest-neighbour blockiness. No engine can do better than its source material
allows. On a modern high-DPI display the result is unusable, and no amount of
work on the engine changes that.

So Ferrolux draws instead. Every element of the control surface is vector
geometry or a fragment shader. Resolution independence stops being a feature to
be engineered and becomes a structural property — and the visual identity that
skinning would have provided becomes the project's own responsibility, which is
the intended trade. This is recorded as
[D-003](docs/DECISIONS.md), and it is the decision everything else follows from.

## The panel

The interface is modelled on late-1970s and 1980s high-end tape decks: a warm
off-white chassis, amber readouts, chunky moulded controls with visible travel,
and meters that behave like instruments rather than animations.

That last point is where most of the effort goes. The VU needle is a second-order
system reaching 99% deflection at 300 ms with 1–1.5% overshoot, per IEC 60268-17
— because instantaneous RMS looks twitchy and reads immediately as a fake. The
lag, and the slight settle-back after it, are the entire character of the
instrument. Measured: 302.0 ms and 1.16%.

Behaving like an instrument has to be affordable as well as correct. Every
display is measured at 3840×2160 against a 60 fps budget by
`tools/measure-frames.sh`, which renders the real meter QML offscreen with a
fence after each frame so the headroom figure is the true one rather than
whatever the compositor's pacing allows it to look like. That measurement caught
the flame display running at 37 fps at 4K, where it had passed every test that
could only reach 1080p.

A static mockup of the intended layout is at
[`docs/cassette_futurism_player_ui_mockup.html`](docs/cassette_futurism_player_ui_mockup.html).
There are no screenshots of the panel because it does not exist yet; what runs
today is a plain Qt Quick window: transport buttons, a position bar and a
playlist in the default style, which is exactly what a harness should be.

## What it will do

| | |
|---|---|
| **Playback** | FLAC, MP3, Ogg Vorbis, Opus, AAC/M4A, WAV, AIFF, WavPack, Musepack, ALAC. Gapless. Cubic volume taper, constant-power balance. |
| **Playlist** | 20,000 entries at 60 fps. Multi-select, drag reorder, undo. Shuffle as a permutation, not a per-track dice roll. M3U, M3U8 and PLS. |
| **Equaliser** | Ten bands at the classic centre frequencies, ±12 dB, with preamp and headroom management. Winamp `.eqf` preset import. |
| **Displays** | Spectrum bars, mirrored spectrum, flame, stereo VU needles and an LED peak ladder, all shader-rendered from a shared meter source. |
| **Desktop** | MPRIS2, media keys under X11 and Wayland, single-instance enqueue, session restore. |

Deliberately **not** in scope: Winamp skin compatibility, streaming services,
video, a library database, tag editing, or a visualisation plugin API. Each
exclusion is reasoned in [FEATURES.md](docs/FEATURES.md).

## Built on

Qt 6.4 with QML for presentation, C++20 for the engine, GStreamer 1.20 with
`playbin3` for audio, and TagLib for metadata from Phase 2. Linux only — X11 and
Wayland both supported, with platform-specific code confined to one directory so
a future port is bounded work rather than a rewrite.

The reasoning for each of these, and the alternatives rejected, is in
[DECISIONS.md](docs/DECISIONS.md).

## Status

| Phase | Scope | State |
|-------|-------|-------|
| 1 | Transport core | **Complete** 2026-09-02 |
| 2 | Playlist | Feature-complete 2026-09-02 |
| 3 | Equaliser | Feature-complete 2026-09-02 |
| 4 | Meters | **Complete** 2026-09-03 |
| 5 | The panel | In progress |
| 6 | Desktop integration | Not started |
| 7 | RS-1 release | Not started |

Audio correctness comes before appearance. The panel is the point of the
project, but a beautiful panel over a pipeline that stutters is not shippable,
and meter work is meaningless until there is a signal to meter. Full plan in
[ROADMAP.md](docs/ROADMAP.md).

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ferrolux ~/Music/some-album/track.flac
```

Dependencies, per-distribution package lists and troubleshooting are in
[BUILD.md](docs/BUILD.md), written from the first working build rather than from
intention. Phase 1 needs Qt 6 Base and Declarative, GStreamer 1.20 and its base,
good and bad plugin sets; TagLib and Qt Shader Tools arrive with later phases.

## Documentation

The design documentation is the larger half of this repository and is meant to
be read, not skimmed.
[`docs/README.md`](docs/README.md) is the index; the entries most worth reading
first are [DECISIONS.md](docs/DECISIONS.md) for why the project is shaped the
way it is, [SPEC.md](docs/SPEC.md) for the constants that define its behaviour,
and [ATTACK_VECTORS.md](docs/ATTACK_VECTORS.md) for the failure modes it is
built to survive.

Constants are not repeated here. SPEC.md is authoritative for every value; this
page links rather than restates, so the two cannot disagree.

## Licence

**GPL-3.0-or-later.** The full text is in [LICENSE](LICENSE), and every source
file carries an `SPDX-License-Identifier` header.

You may use, study, modify and redistribute Ferrolux. If you distribute it, or
anything derived from it, you must pass on the same freedoms and provide
complete corresponding source. It cannot be incorporated into proprietary
software.

Nothing in the dependency set forced this — Qt, GStreamer and its plugins are
all LGPL — so it is a deliberate choice rather than an obligation. The
reasoning, including why copyleft is *less* packaging work here than a
permissive licence would be, is recorded as [D-010](docs/DECISIONS.md).

The bundled fonts are separately licensed under the SIL Open Font License 1.1
and are redistributable on their own terms. See [D-012](docs/DECISIONS.md).

---

*RS-1 is the model code on the panel badge and denotes the hardware
generation, not the software revision. Releases follow semantic versioning —
see [D-008](docs/DECISIONS.md).*
