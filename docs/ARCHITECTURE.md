# Architecture

Descriptive document: the system as it is intended to be. Rationale for these choices lives in [DECISIONS.md](DECISIONS.md), not here.

Ferrolux RS-1 is planned rather than built. This document describes the target structure that Phase 1 through Phase 5 implement; it should be revised to describe reality as soon as reality diverges.

---

## System overview

```
                         ┌──────────────────────────┐
                         │        qml/ (UI)         │
                         │  panel · meters · shaders│
                         └─────┬──────────────┬─────┘
                properties &   │              │  texture
                signals        │              │
                     ┌─────────▼──────┐  ┌────▼─────────────┐
                     │  core/Engine   │  │ meters/          │
                     │  core/Equaliser│  │  MeterSource     │
                     └─────────┬──────┘  │  MeterTexture    │
                               │         └────▲─────────────┘
                        control│              │ bus messages
                               │              │
                     ┌─────────▼──────────────┴─────────────┐
                     │        GStreamer pipeline            │
                     │  playbin3 → audio-filter chain       │
                     └──────────────────────────────────────┘
                               ▲
                        URIs   │
                     ┌─────────┴──────┐     ┌────────────────┐
                     │ library/       │     │ platform/      │
                     │  PlaylistModel │     │  Settings      │
                     │  Metadata      │     │  Mpris         │
                     │  PlaylistIO    │     │  SingleInstance│
                     └────────────────┘     └────────────────┘
```

The UI is a consumer. It reads state and pushes commands; it holds no playback logic of its own. Every arrow into `qml/` carries data that is already in its final display form, so that a shader or a delegate never has to compute anything non-trivial during a frame.

---

## Data flow

### Audio path

```
file → playbin3 → audioconvert → equalizer-10bands → level → spectrum
     → audioconvert → audioresample → autoaudiosink
```

`level` and `spectrum` are both pass-through elements, so they sit in series in the main chain rather than behind a `tee`. Neither modifies the signal. They post messages on the pipeline bus, which is where meter data enters the application.

The equaliser sits before the analysis elements deliberately: the meters show what is being heard, not what was decoded.

### Meter path

Bus messages arrive on the application's main loop, not the streaming thread. `MeterSource` converts them into display-ready state:

1. Spectrum magnitudes are bucketed from the analysis band count down to the display band count using the logarithmic mapping in [SPEC.md](SPEC.md) §Meters.
2. Exponential smoothing is applied per band.
3. Peak-hold caps are updated and decayed.
4. RMS values are integrated through the VU ballistics filter.

`MeterTexture` is a `QQuickItem` that owns an `N×1` two-channel texture. On each render pass it uploads the current band state — red channel current magnitude, green channel peak-hold cap — and exposes the texture as a property to the `ShaderEffect` instances in `qml/meters/`. Linear filtering on the texture gives interpolation between bars without any CPU cost.

### Oscilloscope path (candidate, F-034)

Time-domain display needs raw PCM, which the level/spectrum path does not carry. If F-034 is promoted, a `tee` after the equaliser feeds an `appsink` with a small ring buffer. This is the only place in the design where audio samples cross into application memory, and it is why the feature is a candidate rather than a commitment.

---

## Module responsibilities

**`core/`** owns the pipeline and everything that changes what comes out of the speakers. `Engine` builds the pipeline, runs the playback state machine, and is the sole owner of every GStreamer object; nothing outside `core/` holds a `GstElement*`. `Equaliser` is a thin abstraction over the filter element, exposing bands and preamp as gains in decibels rather than as backend-specific properties, so the backend can be replaced without touching the UI (see D-005).

**`meters/`** turns bus traffic into pixels. `MeterSource` handles acquisition, bucketing, smoothing and ballistics; `MeterTexture` handles GPU residency. The split matters because ballistics are physical modelling that belongs on the CPU where it can be tested, while rendering is per-fragment work that belongs on the GPU. No module other than `meters/` knows the texture layout.

**`library/`** owns the list of things to play. `PlaylistModel` is the single source of truth for playlist contents and play order; `Engine` is told what to play, it does not decide. Metadata extraction runs on a worker thread and populates rows by signal, so adding ten thousand files never blocks the interface. `PlaylistIO` handles M3U and PLS serialisation.

**`platform/`** contains everything that is about the desktop rather than about audio: settings persistence, the MPRIS2 D-Bus service, media key handling, single-instance coordination and command-line parsing. Isolating it means the rest of the application has no direct dependency on D-Bus or on the session type.

**`qml/`** is presentation only. `panel/` holds the chrome and controls, `meters/` holds one component per display mode, `shaders/` holds the GLSL sources compiled by `qsb` at build time. Display modes are swapped by a `Loader` over a common `MeterData` context property, so adding a mode means adding one component and one shader, with no change anywhere else.

---

## Key invariants

1. **The streaming thread does no application work.** No allocation, no locking against UI state, no signal emission that could reach the main thread synchronously. Violations show up as audio dropouts under load, not as crashes. See AV-001.
2. **GStreamer object ownership is confined to `core/`.** Any code outside that directory holding a raw pipeline reference is a defect regardless of whether it currently works.
3. **Texture uploads happen only on the render thread.** `MeterTexture` does its work inside the scene graph's synchronisation and render callbacks, never from a bus handler. See AV-007.
4. **Playback position is polled once per rendered frame, maximum.** Every consumer of position reads the same cached value. Multiple independent pollers are a performance defect and a source of visible disagreement between UI elements.
5. **The playlist model owns play order.** Shuffle is a permutation held by the model, not a random choice made at advance time. Nothing else may decide what plays next.
6. **No fixed-size bitmap assets in the control surface.** Chrome is vectors and shaders. Photographic textures for panel finish are permitted only as tiling detail that is scale-invariant in appearance. See D-003.
7. **Equaliser changes never restart the pipeline.** Gain changes are property writes on a live element, interpolated over the ramp time in SPEC.md to avoid zipper noise.
8. **Every gain path has stated headroom.** The combination of preamp, band gains and volume must be provably incapable of hard clipping. See AV-003.

---

## Cross-cutting concerns

**Threading model.** Three thread contexts matter. The GStreamer streaming threads are owned entirely by the pipeline and must be treated as untouchable. The Qt main thread runs the application logic, bus message handling and the QML engine. The Qt scene graph render thread runs shader rendering and texture uploads. Metadata extraction adds a fourth context, a `QThreadPool` worker set, which communicates only by queued signal.

**Error handling.** GStreamer errors arrive as bus messages and are translated into application-level states by `Engine`. A file that fails to open is a recoverable condition that advances the playlist; a pipeline that fails to reach `PLAYING` is a fatal condition for that pipeline and triggers a rebuild. No error path silently swallows a message — anything not handled specifically is logged with the element name and the GStreamer domain.

**Logging.** Qt's categorised logging, one category per module (`ferrolux.core`, `ferrolux.meters`, `ferrolux.library`, `ferrolux.platform`). Meter and position logging is off by default because both fire at frame rate.

**Settings.** `QSettings` in INI format under `$XDG_CONFIG_HOME/ferrolux/`. Key names are specified in SPEC.md §Settings so that they are stable across versions and can be migrated deliberately rather than accidentally.

**Theming.** Panel appearance is a token set — colours, radii, bevel depths, type scale — resolved at load time. Components reference tokens, never literal values. This is what makes F-044 a token swap rather than an asset pack.
