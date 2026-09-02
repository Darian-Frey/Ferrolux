# Ferrolux

**A cassette futurism audio player for Linux.**

Transport, playlist, a ten-band equaliser and switchable VU and spectrum
displays — the scope Winamp had — rendered as resolution-independent vector
chrome rather than bitmap skins, so the panel is correct at any display scale.

> **Status: pre-implementation.** This repository currently contains design
> documentation and nothing else. There is no source code, no build system and
> no executable. Phase 1 has not started. See [Status](#status) below.

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

That last point is where most of the effort goes. The VU needle is specified as
a first-order system with a 300 ms integration time and 1–1.5% overshoot, per
IEC 60268-17 — because instantaneous RMS looks twitchy and reads immediately as
a fake. The lag is the entire character of the instrument.

A static mockup of the intended layout is at
[`docs/cassette_futurism_player_ui_mockup.html`](docs/cassette_futurism_player_ui_mockup.html).
There are no screenshots, because there is nothing to screenshot yet.

## What it will do

| | |
|---|---|
| **Playback** | FLAC, MP3, Ogg Vorbis, Opus, AAC/M4A, WAV, AIFF, WavPack, Musepack, ALAC. Gapless. Cubic volume taper, constant-power balance. |
| **Playlist** | 20,000 entries at 60 fps. Multi-select, drag reorder, undo. Shuffle as a permutation, not a per-track dice roll. M3U, M3U8 and PLS. |
| **Equaliser** | Ten bands at the classic centre frequencies, ±12 dB, with preamp and headroom management. Winamp `.eqf` preset import. |
| **Displays** | Spectrum bars, mirrored spectrum, stereo VU needles and an LED peak ladder, all shader-rendered from a shared meter source. |
| **Desktop** | MPRIS2, media keys under X11 and Wayland, single-instance enqueue, session restore. |

Deliberately **not** in scope: Winamp skin compatibility, streaming services,
video, a library database, tag editing, or a visualisation plugin API. Each
exclusion is reasoned in [FEATURES.md](docs/FEATURES.md).

## Built on

Qt 6.5 with QML for presentation, C++20 for the engine, GStreamer 1.20 with
`playbin3` for audio, and TagLib for metadata. Linux only — X11 and Wayland
both supported and tested, with platform-specific code confined to one
directory so a future port is bounded work rather than a rewrite.

The reasoning for each of these, and the alternatives rejected, is in
[DECISIONS.md](docs/DECISIONS.md).

## Status

| Phase | Scope | State |
|-------|-------|-------|
| 1 | Transport core | Not started |
| 2 | Playlist | Not started |
| 3 | Equaliser | Not started |
| 4 | Meters | Not started |
| 5 | The panel | Not started |
| 6 | Desktop integration | Not started |
| 7 | RS-1 release | Not started |

Audio correctness comes before appearance. The panel is the point of the
project, but a beautiful panel over a pipeline that stutters is not shippable,
and meter work is meaningless until there is a signal to meter. Full plan in
[ROADMAP.md](docs/ROADMAP.md).

## Building

Not yet possible. When Phase 1 lands:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ferrolux ~/Music/some-album/
```

Toolchain versions and per-distribution package lists are in
[BUILD.md](docs/BUILD.md), which is provisional until the first build actually
succeeds.

## Documentation

The design documentation is complete and is the substance of this repository.
[`docs/README.md`](docs/README.md) is the index; the entries most worth reading
first are [DECISIONS.md](docs/DECISIONS.md) for why the project is shaped the
way it is, [SPEC.md](docs/SPEC.md) for the constants that define its behaviour,
and [ATTACK_VECTORS.md](docs/ATTACK_VECTORS.md) for the failure modes it is
built to survive.

Constants are not repeated here. SPEC.md is authoritative for every value; this
page links rather than restates, so the two cannot disagree.

## Licence

**Not yet chosen**, which means no permissions are granted to anyone at
present — not to use, copy, modify or redistribute. Qt's open-source licensing
and the GStreamer plugin sets in use both constrain the decision, and it must
be settled before the first public commit of source code. Tracked as
[D-010](docs/DECISIONS.md), recorded as Proposed rather than Accepted so the
gap stays visible.

Bundled fonts are a separate question and already settled: all four are SIL
Open Font License 1.1, redistributable under whatever D-010 concludes. See
[D-012](docs/DECISIONS.md).

---

*RS-1 is the model code on the panel badge and denotes the hardware
generation, not the software revision. Releases follow semantic versioning —
see [D-008](docs/DECISIONS.md).*
