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

### BUG-027 A corrupt *next* entry truncates the track that is playing
**Status:** open
**Severity:** low
**Found:** 2026-09-07, fixing BUG-025
**Related:** F-005, F-001, BUG-025

Playing a good file whose *next* playlist entry does not exist stops playback
part-way through the good one. Measured: `format.ogg` played to 4.0 s, the
gapless preload of the missing entry reported `Resource not found`, and the
engine went to `Error`.

The cause is that `playbin3` reports a failure to prepare the *next* URI on the
same bus as a failure of the one playing, and `Engine::fail` cannot tell them
apart — so a fault in a file nobody is listening to yet ends the one they are.
F-005 caches the next URI ahead of time precisely so the streaming thread never
has to ask for it, which is what makes the failure arrive early and out of
context.

**It predates BUG-025's fix**, confirmed by stashing that work and reproducing
the same six errors and the same stop on the committed code. BUG-025's advance
makes the aftermath tidier — the playlist moves on rather than sitting there —
but the good track is still cut short.

**Investigated 2026-09-07 and not fixed.** The diagnosis is complete and two of
the three pieces of a fix are proven; the third is not, and shipping the first
two alone would trade one failure mode for another.

*Measured damage.* A 6.97 s file whose next entry is missing plays to about
5.x s — cut short by roughly the `about-to-finish` lead — and then stops without
advancing. With a good next entry the same file reaches 6.x s and hands over
normally. A **corrupt** next entry does exactly what a missing one does, so
checking that the file exists before arming the handover would cover only half
the cases.

*An error can be attributed to its URI.* Walking up from `GST_MESSAGE_SRC`
through `gst_object_get_parent` to the nearest object with a `uri` property
returns the URI the failing element belongs to — proven with a probe: while
`format.ogg` played, all six errors reported the *next* file's URI and none the
current one. That is the discrimination `Engine::fail` lacks.

*Ignoring them preserves the track.* With next-URI errors ignored, the current
file played its full 6.96 s with zero current-stream errors.

*But nothing then ends the stream.* Over a 45-second window after a failed
handover, **no EOS ever arrives** — so the track finishes, the pipeline sits
there, and the playlist never advances. Ignoring alone converts "cut short and
stopped" into "complete and stuck", which is not obviously better.

*Repair is possible and delicate.* Handing over a different file after the
failure does start a new stream, but the classification above stops being
reliable once `playbin`'s `uri` property has been changed underneath it — the
property is shared state, and errors still draining from the old attempt then
attribute to the new URI. That is the part needing a design rather than a patch:
`Engine` would have to track the prepared source itself instead of asking the
pipeline what it currently holds.

**Partly fixed 2026-09-07, and the severity is now much lower than this entry
first claimed.** Two changes have happened since it was written and both matter.

BUG-025's advance means playback no longer *stops*: with a third entry after the
broken one, the player skips the bad file and plays on. The title above
overstates what remains.

And `Engine::setNextSource` now declines to hand over a next source that cannot
be opened — checked on the main thread with one `stat`, never in
`aboutToFinish`, which runs on a streaming thread and may not touch the
filesystem (AV-001). Declining costs nothing: the stream finishes normally, posts
EOS, and the playlist advances onto the unusable entry in the ordinary way, where
it fails as the *current* source and is stepped over. The file is skipped either
way; this is about not damaging the track before it.

Measured on a 6.966 s file whose next entry is missing: **6.965 s of 6.966
before it hands on**, against about 5.3 s and a stop before these two changes.

**What remains is the corrupt-but-readable next file**, which still truncates —
5.035 s of 6.966 measured — and then advances correctly. A `stat` cannot tell
that a file will not decode, so the check above cannot catch it; only the URI
attribution can, and that still runs into the missing EOS described above.

Left open at **low severity**, precisely scoped: a next entry that exists, is
readable, and does not decode costs the previous track its last two seconds. The
common case — a playlist pointing at files that have been moved or deleted — is
fixed.

## Fixed

### BUG-031 Three measurement tools wrote a settings file the application does not read
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-08, capturing the VU face and finding the mode setting ignored
**Fixed:** 2026-09-08
**Related:** F-004, AV-001, AV-007, F-033, IMP-010

`stress-audio.sh`, `verify-render-thread.sh` and `verify-mode-switch.sh` each
begin by writing `$XDG_CONFIG_HOME/ferrolux/ferrolux.conf` to set
`playback/volume` to zero. The application uses `QSettings::IniFormat` with an
application name of `ferrolux`, so it reads `ferrolux.ini`. The file those tools
wrote was never opened.

Every run of all three therefore played at the default volume of 0.7, and each
tool's header — and BUILD.md — said in as many words that it plays at volume
zero and is silent. The measurements are unaffected: muting is downstream of the
streaming thread, the render thread and the frame clock alike, and the underrun
counts come from the sink either way. What was affected is the room the machine
is in, over two 45-second runs, two 12-second runs and two 20-second runs.

Found by accident, which is the part worth recording. The same mistake in a
capture script meant `meters/mode` was ignored too, so a screenshot taken to
inspect the VU face came back showing the spectrum — the wrong display was the
symptom that led to the wrong filename. Nothing else would have reported it:
`XDG_CONFIG_HOME` pointed at a temporary directory, the file was written without
error, and a player that ignores a settings file it cannot find starts perfectly
happily on its defaults.

`verify-desktop.sh` and `verify-scaling.sh` had the name right all along, which
is how the wrong one went unnoticed beside them.

### BUG-030 The VU face's scale marks are evenly spaced, so it does not crowd as SPEC.md says it does
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-08, working on F-032's outstanding clause
**Fixed:** 2026-09-08
**Related:** F-032, AV-012, SPEC.md §Meters, ROADMAP.md Phase 4

SPEC.md §Meters says: "Deflection is linear in amplitude rather than in
decibels, which is why a VU scale crowds towards its left end as a real one
does." The first half is implemented and now checked across the whole range —
`meters_test` holds the needle to the voltage law at eight marks from −20 dB to
+3 dB. The second half is not implemented at all.

`qml/shaders/vu.frag` draws its ticks as `fract(sweep * 8.0)`: eight marks
evenly spaced along the sweep, which is to say evenly spaced in *amplitude*.
Nothing crowds. There are no numerals anywhere on the face either, in the shader
or in `MeterDisplay.qml`.

Because deflection is linear in amplitude, a decibel mark belongs at
`10^(dB/20)` of the sweep, and the standard VU face marks these:

| dB | −20 | −10 | −7 | −5 | −3 | −2 | −1 | 0 | +1 | +2 | +3 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| position | 0.100 | 0.316 | 0.447 | 0.562 | 0.708 | 0.794 | 0.891 | 1.000 | 1.122 | 1.259 | 1.413 |

That is the crowding, and the size of it: ten decibels occupy the first fifth of
the arc while the last three occupy nearly a third. It is the single most
recognisable thing about a VU face — a viewer identifies the instrument by the
bunched marks at the left long before reading any number on it.

**This matters because of which clause it blocks.** Phase 4's acceptance, carried
into F-032's status, asks that the needle be *visually indistinguishable from a
reference deck fed the same programme material*. That has stood as a judgement
nobody could make into a measurement — but part of it is not a judgement at all.
A deck's face crowds; this one does not; and no amount of looking at the two will
make them the same instrument until it does. What is genuinely aesthetic — the
colour of the ink, the shape of the needle, whether the numerals are drawn and in
which face — is what should be left to the author's eye.

The three clauses listed under F-032 itself are all met, and this is not one of
them. It is the phase clause, and this entry is what stands between it and a
decision.

**Fixed to the author's choice: marks at the standard positions, no numerals.**
The crowding is what identifies the face, and type legible at 3× device pixel
ratio in a full-height panel is not legible in compact mode at 1× — F-042 folds
this display to a fraction of its height. The marks carry the shape; the
readouts carry the numbers. The arc's travel was extended from 1.4 to 1.413,
which is +3 dB exactly, so the last mark falls on the end of the scale rather
than just outside it; the difference is under a pixel, but it was the one place
on the face where the geometry did not come from the law.

**Two further defects surfaced while looking at the result, both older than this
entry.**

The marks were rendering as faint specks rather than as ticks, in the corrected
face and in the original alike. The radial extent read `(1 - smoothstep(r -
0.075, r - 0.070, radius)) * step(r - 0.075, radius) * ...`, and those first two
factors are non-zero together only between `r - 0.075` and `r - 0.070` — a
sliver a fifteenth of the length the constant plainly intends. A scale that
crowds correctly is no use if it cannot be seen to.

And testing eleven marks per fragment across the whole sweep cost **2.5 ms of a
16.7 ms frame at 3840×2160**, taking the VU mode from 40.6% headroom to 25.3%
and failing AV-002's 30% floor outright. `measure-frames.sh` caught it on the
first run after the change. The band is a thin annulus, so computing it before
the loop and skipping the loop where it is zero lets almost every fragment in
the face leave without testing anything: the mode now runs at **8.961 ms and
46.2% headroom**, better than the 9.897 ms it managed before any of this.

### BUG-029 `verify-desktop.sh`'s session check races a track boundary and fails about half the time
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-08, adding F-004's persistence coverage
**Fixed:** 2026-09-08
**Related:** F-015, IMP-010, AV-006

The session section of `tools/verify-desktop.sh` reads the current track and
position over MPRIS, quits the player, restarts it, and asserts that both come
back. Playback continues between the read and the quit landing — the script says
so, and tolerates the position being *a little later* than the figure it
observed. What it does not tolerate is the track having changed, and about half
the time it has.

The material is six tracks of nine seconds. By the time the session section
runs, earlier checks have left playback partway through one, and `call Play;
sleep 4` then puts the observed position at **9.12 s of a 9 s track** — that is,
at the boundary, every run. Whether the handover falls before or after the quit
is a coin flip, and the two symptoms seen are the two sides of it: the restored
track not matching the one read, and the restored position being 0.

**The player is right in both cases.** `Engine::position()` returns a cached
value that a gapless handover resets to zero (`Engine.cpp`, the
`gaplessAdvance` path), so a session saved just after a boundary correctly
records the *new* track at position 0, and restore correctly brings back exactly
that. The log from a failing run says `restored 6 entries at row 5` and the
settings file says `track=5, position=0`, which is a faithful account of where
playback was — not of where it was four seconds earlier when the script looked.

So this is a false failure, and false failures are worse than no check: this one
sits in the tool that F-004, F-015 and every settings key are now verified by, and
the natural response to a check that fails half the time is to stop reading it.

**Fixed by placing playback rather than finding it.** The section now uses
MPRIS `SetPosition` to put the stream two seconds into the current track before
taking its reading, which leaves the entire window between the reading and the
quit clear of the boundary. Nine consecutive runs pass, against roughly one in
two before.

Three ways out were considered. Pausing first would make the section
deterministic by no longer testing what F-015 is about, which is quitting *while
playing*. Comparing the restored state against the saved settings file instead
of against a live reading would remove the race, but puts the file on both sides
of the comparison and so weakens what is being asserted. Placing the position
keeps a playing quit and keeps the comparison honest, and costs one extra
check — that the placement actually happened, because otherwise a `SetPosition`
that silently stopped working would leave everything below passing while
measuring exactly the race this removes.

Nothing in F-015 changed. The player was correct throughout, which is the
uncomfortable part: the tool that verifies the session, the settings and now
F-004 spent an unknown number of runs reporting a defect in code that had none,
and the only reason it was caught is that a new section was added beside it and
the failure had to be attributed before that work could be reported.

### BUG-028 `frame_bench` measured shaders with undefined parameters
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-07, measuring F-031's 60 fps clause at 4K
**Fixed:** 2026-09-07
**Introduced:** 2026-09-06 by F-036, in a file F-036 did not touch
**Related:** F-031, F-036, F-033, AV-002, BUG-016, IMP-003

`tests/frame_bench.cpp` builds its own `QQmlApplicationEngine` and registers the
context properties the display needs — but not `Visuals`. Every `Visuals.*`
binding in the shader items therefore raised `ReferenceError: Visuals is not
defined`, and each affected uniform took the default of its declared type
instead: zero for the numbers, which is not a number the panel can ever produce.

So the benchmark ran, reported plausible per-frame figures, and passed. What it
did not do is measure the shaders the application draws.

The size of the error depends on which parameter went missing. For the flame it
is the whole result: `Visuals.flameRanks` sets how many layers the fragment
shader composites, which BUG-016 established is what dominates that shader's
cost. Measured on the reference hardware at 3840×2160:

| | mean frame |
|---|---|
| flame, `Visuals` undefined | 4.292 ms |
| flame, `Visuals` bound | 7.374 ms |

The benchmark had been understating the most expensive mode in the player by
**72%**, and it was the tool the 60 fps clauses of F-031, F-032 and F-033 were
all going to be judged by. Both figures pass, which is why nothing looked wrong;
had the flame been near the budget, this would have reported headroom that did
not exist and the failure would have been found by somebody watching it stutter.

Fixed by constructing a `VisualSettings` in the benchmark and setting it as the
`Visuals` context property, alongside the ones already there, and adding
`src/ui/VisualSettings.cpp` to the target.

**It is a regression, and the shape of it matters more than the size.** The
benchmark was correct when Phase 4 closed: the shaders carried their proportions
as literals then, so there was no context property to be missing. F-036 turned
those nine literals into `Visuals.*` bindings on 2026-09-06 — a change entirely
within `qml/` and `src/ui/`, which broke a test in `tests/` that neither
compiles against them nor mentions them. Nothing could have failed: the coupling
is by name at run time, and the failure mode of a missing name in QML is a
default, not a stop. The Phase 4 figures in ROADMAP.md and AV-002 are therefore
sound as measured; what was invalidated is every measurement taken in the day
between F-036 and this entry, which is why F-031's status quotes a re-measurement
rather than the run that found the bug.

**The general lesson is the second half of IMP-003's.** A missing context
property in QML is not an error the process notices — it is a message on stderr
and a default value, and a benchmark is precisely the kind of program nobody
reads the stderr of. Two of the three measurement tools now in `tools/` have
been wrong in a way that made the product look better than it was, and in both
cases the number was believable. A measurement that cannot fail loudly should be
sanity-checked against a figure obtained another way before it is quoted in an
acceptance status, which is how this one was caught: the flame is visibly the
heaviest mode and was reporting the cheapest number.

### BUG-026 `errorBanner` is called twice and does not exist
**Status:** fixed
**Severity:** low
**Found:** 2026-09-07, deciding where a playback error should be shown
**Fixed:** 2026-09-07
**Related:** F-022, BUG-025

`Main.qml` calls `errorBanner.show(...)` in two places — when a preset name
contains a slash, and when an `.eqf` import fails — and nothing anywhere defines
`errorBanner`. Both are `ReferenceError` at the moment they are needed, so the
two failures they exist to report are silent.

Neither path runs at startup, which is why nothing has ever shown it: the id is
only resolved when the code is reached, and both are reached only on a failure
somebody has to cause on purpose.

The gap is wider than the two calls. There is no transient notification surface
at all — the panel has exactly one place for a message, the display's second
line, and it is bound to `Engine.errorText`. That is why BUG-025 had to hold its
error there on a timer: with a banner it would have been the banner's job.

Not fixed inline. It is a new component and a decision about where notifications
belong on a panel that has so far had none.

**Fixed by using the surface the panel already had**, rather than by adding the
banner the two call sites were named after. `DisplayPanel` puts an error where
the album would be, lit rather than dimmed, on its own stated principle that "an
error takes this line rather than getting one of its own" — a panel with one
place for a message should not grow a second one the first time something else
needs to say something. `errorBanner` now holds the text and that line shows it,
exactly as it shows the engine's, with the engine's winning where both have
something: those are about the thing the instrument is for.

It holds for six seconds, matching `Engine::kErrorHoldMs`, so a message from
either source stays the same length of time and the panel does not appear to
have two clocks.

**A second defect on the same line, found by looking at the result.** The
message arrived as "A preset needs a name without a …", cut off before the part
saying what to do about it. `albumReadout` is anchored right to
`formatReadout.left`, and when an error is shown `formatReadout` is *hidden* —
but an invisible item still holds its anchor, so the message was being elided to
leave room for a field that is not drawn. It now anchors to the counter instead
while an error is up.

That one predates this entry and applied to the engine's errors too: every
message longer than about thirty characters was truncated at half the display's
width, on a display with the room to show it. It was invisible because the
messages that had been seen were short enough to fit.

Verified by driving the failure rather than reasoning about it: saving a preset
named `a/b` puts the full sentence on the display and clears it six seconds
later, with no `ReferenceError` in the log, and a corrupt file still shows the
engine's own error on the same line.


### BUG-025 A file that will not load stalls the playlist instead of advancing
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-07, verifying F-001's format list
**Fixed:** 2026-09-07
**Related:** F-001, F-010, BUG-024

F-001's second acceptance clause: "unsupported or corrupt files produce a
visible error and advance the playlist rather than stalling". The first half
holds — the display prints the engine's error text, and it is legible. The
second half fails in three cases out of four.

Each trial put one bad file ahead of a good one and pressed play:

| the bad file | advanced? | reported |
|---|---|---|
| random bytes named `.flac` | **no** | `Playing`, position 0 |
| a FLAC truncated mid-stream | **no** | `Playing`, stuck at 1.04 s |
| a WavPack file (BUG-024) | yes | correct |
| a path that does not exist | **no** | `Stopped`, position 0 |

The one that advances is the one that fails *early*, at the typefinder. The
three that stall fail later — inside a decoder, at the end of truncated data,
or in the filesystem — and nothing turns any of those into the advance the
clause requires.

**The reported state is worse than the stall.** In two of the three the engine
reaches `Error` and then returns to `Loading`, which `MprisService` maps to
`Playing` because loading is normally on its way to playing. A load that has
already failed is not on its way to anything, so every desktop control and lock
screen shows a player playing a track whose position never moves. The panel is
honest — it prints the error — and the desktop is not.

Not fixed inline, per Maintenance Rule 8. It is a change to the engine's error
handling and to what `Loading` is allowed to mean, and both deserve a decision
rather than a patch.

**Fixed 2026-09-07**, and the table above was partly wrong. Two of its four rows
were artefacts of the test rather than defects.

`addPaths` **sorts**, so a bad file named `no-such-file.flac` sorted last and had
nothing to advance *to*. Renaming it so it sorted first showed it advancing
correctly all along. And the truncated FLAC is not broken in the way it looked:
its header declares the full 32.5 seconds, so the player honours that, plays
silence past the cut and advances at the end. What looked like a stall was a
file playing exactly as its own header describes it.

The two real defects were both in the engine.

**A file that fails to load is never retried into working.** `play()` on a
source already in `Error` set the state back to `Loading` and asked the pipeline
to start; nothing new was attempted, so no new error arrived, and the player sat
in `Loading` for ever — which MPRIS reports as playing, with a position that
never moves. Asking to play something that has already failed is itself a failed
attempt, and `Engine::play` now says so rather than pretending. Nothing is lost
by not retrying: a file that failed to load will fail again.

**Nothing turned a failure into an advance.** `Engine` now emits `sourceFailed`
with the file, the message and whether playback had been asked for, and `Player`
steps past it. A track that was merely *selected* and turns out to be broken
stays selected — nothing was playing, so there is nothing to carry on from. The
advance is queued rather than direct, so a run of broken files does not recurse
once per file, and it stops after one full pass: every entry having failed means
there is nothing to advance to, and under repeat-all the alternative is an
endless circuit. Measured: two broken files settle at fourteen GStreamer errors
and stay there, at idle CPU.

**The visible error needed a second attempt.** Clearing it when playback resumed
was correct and useless — a skip takes a fraction of a second, so the message
flashed past unread, which is not the visible error the clause asks for. It is
held for six seconds after playback moves on, and a fresh failure replaces it
and restarts the wait. Confirmed on the panel: three seconds after a skip it
reads `Internal data stream error.` over the track now playing, and at nine
seconds it is gone.


### BUG-024 WavPack is offered and accepted, and cannot be played
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-07, verifying F-001's format list
**Fixed:** 2026-09-07
**Related:** F-001, D-002, BUG-006

`.wv` is in `PlaylistModel::audioSuffixes`, WavPack is in F-001's acceptance
list, and `MprisRoot::supportedMimeTypes` advertises `audio/x-wavpack` to the
desktop — which now also puts it in the `.desktop` entry's `MimeType`, so a file
manager offers Ferrolux as a way to open one. The file is added to the playlist,
the panel shows a visible error, and nothing plays.

**The decoder is present and works.** `wavpackparse ! wavpackdec` decodes the
file perfectly. What fails is the routing to it: GStreamer's typefinder does not
recognise WavPack. Across five test files it reported `video/x-h264` twice and
nothing at all three times, so `decodebin` never reaches the decoder and
`playbin3` reports a stream error from `h264parse`.

It is not the fixture. The file produced by the **reference** `wavpack` encoder
fails exactly as GStreamer's own `wavpackenc` output does — which is why the
reference encoder was installed rather than assumed unnecessary. It is not
element ranking either: `musepackdec` is `marginal` like `avdec_alac`, and both
of those play.

So the fault is upstream, in the same way BUG-006 is, and the question this
entry poses is what Ferrolux should say about it. **Advertising a format in
three places and failing on it is the worst of the options**: the user is
invited to try. Removing `wv` from the suffix list and from the advertised media
types would at least be honest, at the cost of the format never working if the
typefinder is fixed later. Leaving it and detecting the failure well — which
needs BUG-025 first — is the other direction.

Not decided here. Both routes change what the application claims about itself,
which is the author's to settle.

**Fixed by supplying the missing piece rather than by choosing between the two
options above.** Neither was needed: `core/TypeFinders` registers a type finder
that matches the four ASCII bytes `wvpk` at offset zero, and WavPack plays.

The third route was invisible until the failure was understood properly. A
decoder is only reachable if something identifies the stream first, and the two
are registered independently — so a format can be perfectly decodable and
completely unplayable at once, which is exactly what this was. Once that is the
diagnosis, the fix is the identification and nothing else.

It was prototyped before it was recommended. A forty-line program registering
the finder and playing through `playbin3` took a WavPack file to EOS, which is
what turned "this might work" into an option worth taking; the earlier reading
that only two routes existed was made without it.

Registered at `GST_RANK_PRIMARY` rather than higher. If the upstream finder is
ever fixed, both match and GStreamer takes the more confident answer, which
costs nothing — ranking ours above everything would mean out-voting a correct
answer with ours. Four bytes at offset zero is specific enough that a false
positive would have to be a file built to look like one, and the sweep confirms
it: all ten of F-001's formats play, so nothing else is being mis-claimed.

Verified on the reference encoder's output, on a 23 MB WavPack of real music,
and on the generated fixture — each loaded into the player with the position
watched, rather than asked of `gst-launch`.


### BUG-023 The application only ran from its build directory
**Status:** fixed
**Severity:** high
**Found:** 2026-09-07, the first time it was installed — Phase 6's last deliverable
**Fixed:** 2026-09-07
**Related:** F-041, D-002, IMP-010

Copied anywhere other than `build-release/`, the player started, claimed its
single-instance bus name, restored its session, began decoding — and then showed
no window, registered no MPRIS service, and spun one thread at 100% of a core
for as long as it was left running. It printed nothing. `cmake --install` was
enough to trigger it, so **the application had never worked when installed**,
which nobody had noticed because nobody had installed it.

`qt_add_qml_module` embeds this application's own module — the components, the
`Tokens` singleton, and the `qmldir` naming them — under `:/qt/qml/Ferrolux`,
and the resource is linked in. What Qt 6.4's engine does not do is search
`qrc:/qt/qml`. It adds `qrc:/qt-project.org/imports` and the directory the
executable is sitting in, and nothing else; Qt 6.5 added the missing one.

**The build tree hid it exactly.** `qt_add_qml_module` also generates
`build-release/Ferrolux/qmldir` on disk, beside the binary — so the second of
those import paths found it, every time, for five phases. The proof is blunt:
copying that one generated directory next to the moved binary restored it
completely, and adding `qrc:/qt/qml` to the engine's import path fixed all three
locations at once.

Two things about the failure are worth keeping. It was **silent** — no QML
warning, no error, nothing on stderr, because the import failure surfaces
somewhere that does not report. And it was **not a crash but a spin**, which
reads as a hung application rather than a broken one and sends the reader
looking at the render loop.

The general form is the one to remember: **a build tree is not an installation,
and an application that has only ever been run from one has an untested
dependency on it.** Nothing here was wrong in a way any test could see, because
every test ran the binary where it was built.

### BUG-022 `PanelMenu` measured its width in a binding that wrote to what it measured
**Status:** fixed
**Severity:** low
**Found:** 2026-09-06, in the log of the BUG-021 shutdown investigation
**Fixed:** 2026-09-06
**Related:** BUG-020, D-012

Every menu opened logged `Binding loop detected for property "contentWidth"`,
tens of times per opening, from both call sites in `Main.qml` — the sort menu and
the preset menu.

The `contentWidth` binding looped over the options *assigning* `metrics.text` and
reading `metrics.width`, so it wrote to the object it depended on: each
assignment invalidated `metrics.width`, which notified the binding, which ran
again and reassigned. Qt breaks the cycle after a bounded number of passes, which
is why the menus were nevertheless the right width and the defect showed only as
log noise and repeated measurement.

**The obvious fix was wrong, and wrong in a way worth recording.** Moving the
measurement into a function called from `onAboutToShow` — exactly as the flip
decision was moved there when `mapToItem` proved untrackable in a binding — is
what this entry originally proposed. It removes the loop and breaks the menu: the
preset menu, which sits at the bottom of the window, unrolled downward off the
edge of it, hiding four of its nine presets.

The flip reads `implicitHeight`, and a `Popup`'s content is not laid out until
something asks for its width. Instrumented at `onAboutToShow`, the binding
version reports `implicitContentHeight` 259.2 and the function version reports
**0** — so `implicitHeight` is 2, the padding alone, `0 > roomBelow` is false, and
every menu believes it has room below. Calling the function earlier does not
help: measuring at `Component.onCompleted` sets the width (66 px, correct) and
`implicitContentHeight` is still 0 one line later, because the layout has not
been through a polish pass. It is late by a frame, and there is no point in the
handler that is early enough.

**Fixed by keeping it a binding and removing the write instead.** `FontMetrics`
measures through `advanceWidth()`, which returns a width without storing a
string, so there is no state for the binding to invalidate and no cycle to break.
The eager evaluation that lays the content out is retained because that turns out
to be load-bearing rather than incidental.

That leaves one trap in place, and it is D-012's in another costume: a function
call is not a tracked read, so a binding built on `advanceWidth()` would never
notice a change of finish or of scale. `Tokens.readoutText` and
`Tokens.sizeReadout` are therefore read into locals purely to establish the
dependency, and the comment says so, because the two lines look removable and are
not.

Verified with the sort menu opening downward and the preset menu upward, both at
full width with nothing elided, zero binding-loop warnings in the log, and
`tools/verify-scaling.sh` holding at 1×, 1.5×, 2× and 3×.

### BUG-021 A refused close on the settings window stopped the entire application quitting
**Status:** fixed
**Severity:** high
**Found:** 2026-09-06, reported by the author — "the song is still playing even
though the player closed again, it sounds like there is two or three instances
playing"
**Fixed:** 2026-09-06
**Related:** BUG-012, SPEC.md §Settings

Once the settings window had been opened even once, closing the player left the
process running, windowless and audible. Opening the player again started a
second one, and the instances stacked up — three were found running with no
windows at all, which is how the defect was reported.

`SettingsWindow` was declared with a handler that refused its own close, on the
reasoning that hiding it rather than destroying it would preserve the scroll
position:

```qml
onClosing: function (close) {
    close.accepted = false
    settingsWindow.visible = false
}
```

**`Qt.quit()` sends a closing event to every top-level window, and a single
refusal cancels the quit.** Reduced to a thirty-line program with no Ferrolux
code in it, on Qt 6.4.2: a second window with no closing handler quits; one that
accepts quits; one that is *present but empty* quits; one that sets
`accepted = false` hangs forever. Instrumenting both windows shows the order —
the root window receives its closing event and goes, the second refuses, and the
quit is abandoned with the main window already gone. There is then no window
left to close and no way to ask the process to quit again.

That also explains the two observations that looked contradictory during the
investigation. `onClosing: Qt.quit()` on the main window demonstrably *fired*,
yet `aboutToQuit` never did and `app.exec()` never returned — because the quit
was begun and then refused. And hiding the settings window again before closing
the player did not help, because a hidden window is still a top-level window and
still receives the closing event.

**Fixed by deleting the handler.** The premise was wrong: closing a QML `Window`
already only hides it, leaving the object and all its state alive, so nothing had
to be refused to get the behaviour that was wanted. The window still reopens on
the settings key with its state intact after being closed by the window manager,
and closing it still does not quit the player.

Note the symptom is identical to BUG-012 and the cause is unrelated — that one
was `quitOnLastWindowClosed` never being reached because the window was hidden
rather than closed. A player that keeps playing after its window is gone is worth
treating as a class of fault rather than a single bug: **verify shutdown against
every window the application can own, not just the main one.** The check is now
four scenarios — settings never opened, left open, toggled shut, and closed by
the window manager — and all four must reach `aboutToQuit`.

### BUG-019 The equaliser preset name is saved but never restored
**Status:** fixed
**Severity:** low
**Found:** 2026-09-04, while taking screenshots for the README
**Fixed:** 2026-09-05
**Related:** SPEC.md §Settings, F-021, F-022

`equaliser/preset` was written on exit and never read on start. `src/main.cpp`
restored the bands, the preamp and the enabled flag and stopped there, so the
preset field always reported `flat` after a restart — including when the restored
band values were exactly some other preset's.

SPEC.md is why this is a display fault and not a correctness one: "the preset name
is recorded, but the band values are what is authoritative on restore — a preset
may have been edited, or its definition may have changed since it was chosen." The
curve restored was right. What was wrong was the label over it.

**Fixed by not trusting the name.** `Equaliser::adoptPreset` takes a remembered
name as a label only, and only when the curve currently loaded still *is* that
preset: it resolves the name the way `applyPreset` resolves it — a user preset of
the same name shadows a built-in — and compares band by band to within a
hundredth of a decibel. A built-in is judged on its bands alone, because ten bands
is all a built-in defines; a user preset stores eleven values and so puts the
preamp in scope too. Holding a built-in to a preamp it never specified would make
every preset read as edited the moment the preamp moved, which is the opposite of
the fault being fixed.

Restoring the name blindly would have been one line and would have been wrong in
exactly the case the specification bothered to describe.

**A second, quieter fault came out of testing the first.** With the name adopted
only on a match, a curve that had drifted from its preset reported `flat` — the
constructor's default — over somebody else's bands. That is the same class of lie
as the original, wearing a different name. The cause was that `setBands` did not
clear the preset name while `setBand` always had: moving one band away from
`rock` stopped it being rock, and replacing all ten did not. `setBands` now marks
the curve `custom`, and every caller that means to name it — `applyPreset`,
`applyCurve` and the `.eqf` import — sets the name immediately afterwards, so the
change only takes effect where nothing names the curve, which is the restore path.

Seven checks in `tests/equaliser_test`, and verified end to end: a restored `rock`
curve reads `rock`, and the same curve with one band moved reads `edited`.

### BUG-020 The playlist and the preset list are hard to read
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-04, reported from use
**Related:** BUG-017, F-040, SPEC.md §Design tokens, §Typography

Two independent causes with the same symptom, which is why fixing either alone
would have left it half wrong.

**The colour.** `readout-dim` is **3.76:1** against `display-bg`, below the 4.5:1
that normal text needs, and it was carrying the bulk of the panel's text: every
playlist row that was not playing, and every preset that was not selected. The
readout faces are dot-matrix and segmented rather than plain, which asks more of
the contrast rather than less. The current row and the current preset were
`readout` at 6.44:1 — a ratio of only **1.7×**, which is both too little to pick
out at a glance and paid for by making everything around it illegible.

The "one lamp, three brightnesses" idea was mine and it was applied to the wrong
things. A single-colour display does distinguish by brightness, but it does not
dim the text you are meant to read in order to emphasise one line that already
carries a mark of its own. Rows and options are lit at full brightness now; what
is playing is said by the mark beside it, and the preset in effect is *backlit*
rather than merely brighter. A lit ground is unmistakable and costs the other
options nothing.

Dimming is kept for the one case that is about state rather than emphasis: a
missing or unreadable file, which is not a row to be read so much as one to be
noticed as unavailable, and there is no red on this display to say so with.

**The weight.** BUG-017 moved Handjet from `wght` 500 to 300, because at 500 the
elements touch and the face renders as continuous strokes. 300 was an
overcorrection. The separation it buys only exists above roughly 20 units, and a
playlist row is 16 — so in the list it bought nothing and cost stroke weight,
which is exactly where the reading happens. Rendered side by side at 16, 400 is
legibly heavier than 300 and still separates at the sizes where separation is
possible. **`wght` is now 400**, and the instance is named `Handjet Circle Single`.

The two faults compounded. Thin strokes at 3.76:1 is a good deal worse than
either alone, which is why this arrived as "a little difficult to read" rather
than as anything obviously broken — and why the first plausible explanation for
it, taken alone, would have produced a fix that did not work.

### BUG-017 The specified Handjet weight renders the dot-matrix face as continuous strokes
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-03, on the first render of the panel's title readout
**Related:** D-012, F-040, SPEC.md §Typography

SPEC.md instanced Handjet at `wght` 500 and said, in the sentence directly below
the table, that the value is chosen "for a visible gap between neighbours rather
than for stroke weight". At 500 there is no gap. The elements touch, and the face
renders as a condensed sans with notched joins — recognisably not a dot-matrix,
which is the one thing the role was chosen for.

The value was marked **Provisional** pending Phase 5, so this is the phase doing
what it was told to; it is logged rather than quietly corrected because the
document asserted a property its own value did not have, and that is worth being
able to find again.

Rendered across the axis at the sizes the panel uses, 500 never separates and 300
does. Below 300 the separation widens and the readout goes faint, so 300 is the
edge of the useful range rather than a midpoint. **`wght` is now 300.**

A second finding came out of the same render, and it is the more useful one: the
dot-matrix character has a **minimum size**. Below roughly 20 device-independent
units the elements merge at any weight, so a title readout at the 13 the mockup
drew a proportional face at cannot show its dots whatever the instance says.
`size-readout-large` is 20. A dot-matrix face too small to be one is an expensive
way to obtain a plain face.

**The same limit reached the playlist**, found when its rows were first drawn in
the readout face. SPEC.md assigns `type-readout-text` to playlist rows as well as
to the title, so the minimum applies to both — and at the 12 the rows were drawn
at, the face was not merely soft but *ambiguous*: `Reebok` read as `Aeebok`. A
misread character is worse than a plain face, which is the trade the dot-matrix
was chosen against in the first place. Rendered across the range, 14 is
borderline and 16 is where a capital R stops being mistakeable, so `size-readout`
is 16. 20 would be clearer still and would cost a third of the visible rows,
which is a bad trade for a list.

Changing the weight also renames the instance — `fonttools` derives the family
name from the pinned axes, so it is now `Handjet Light Circle Single` rather than
`...Medium...`. The token set, the generator and `tests/tokens_test` all name it,
which is why the test asserts the face Qt actually reports rather than trusting
the filename.

### BUG-018 SPEC.md cited a Reserved Font Name clause that Handjet does not invoke
**Status:** fixed
**Severity:** low
**Found:** 2026-09-03, while bundling the faces
**Related:** D-012, SPEC.md §Typography

SPEC.md said the Handjet instance "is renamed rather than shipped under the
Handjet name" **per the OFL Reserved Font Name clause**. Handjet reserves no
name. Its licence file carries the phrase exactly once, in the OFL's own
boilerplate definition of the term, and its copyright line is bare —
`Copyright 2018 The Handjet Project Authors`, with no `with Reserved Font Name`
following it. DSEG, bundled alongside, does reserve its name and shows what the
declaration looks like when it is there.

Nothing was broken by this, which is the reason to record it. A licence
obligation asserted where none exists is the kind of statement that gets
believed and repeated, and the next person to touch the font pipeline would have
worked around a constraint that was never there.

The instance is still not called plain `Handjet`, on the different and better
ground that it is not Handjet as published: `fonttools` names it for its pinned
axis values, and shipping a modified file under the unmodified name would
misreport what is in the package. Corrected in SPEC.md with the evidence.

### BUG-016 The flame display runs at 37 fps at 3840x2160, and quiet passages are the expensive case
**Status:** fixed
**Severity:** high
**Found:** 2026-09-03, by the AV-002 instrumentation on its first honest run
**Related:** AV-002, D-004, F-035, SPEC.md §Meters

Phase 4 requires every mode to hold 60 fps at 3840x2160 with 30% of the frame
budget spare. The first measurement that could actually reach 4K — the offscreen
`tests/frame_bench` harness — put flame at **26.9 ms per frame, 377 late frames
in 600**. That is 37 fps against a requirement of 60. The other four modes held.

This is precisely the failure AV-002 was written to catch, and it had been in the
tree since flame was added: the mode was measured only at sizes this display can
show, where it holds comfortably, and the cost scales with pixels.

**The cost.** Flame draws nine receding silhouettes, each smoothed with five
texture taps — 45 taps per pixel, at 8.3 million pixels, 60 times a second.

**The trap in it.** The obvious reading is that a loud signal is the expensive
case, because more of the panel is covered. It is the opposite. A tall silhouette
lets a pixel below the nearest crest finish at the first rank, since everything
behind it is hidden; near silence nothing covers anything, no rank can be
dismissed, and every pixel low enough to be within a rank's reach pays for all
nine. The first version of the benchmark fed a loud signal and reported 9.8 ms —
a pass. Sweeping the level from silence to full scale reported 16.5 ms and 77
late frames on the same build. **The benchmark had been measuring the best case
and calling it the average**, which is a worse defect than the one it was hiding.

**Fixed** in four steps, each verified to leave the output pixel-identical:

1. `fwidth(height)` hoisted out of the rank loop — it does not vary by rank.
2. Ranks composited front to back with an *under* operator instead of back to
   front with an *over*. Mathematically identical, but this order can stop once
   the pixel is opaque, and back-to-front cannot: it has to draw every rank
   before it knows which were hidden.
3. A per-rank bound: a rank cannot rise above its own scale, so a pixel above
   that skips the five taps that would have proved it.
4. A whole-pixel bound, which is what actually fixed the quiet case. `MeterSource`
   publishes the frame's tallest band as `ceiling`, and no silhouette can exceed
   `ceiling × backHeight` — the smoothing weights sum to one — so one comparison
   dismisses a pixel that no rank can reach, before any texture is sampled. With
   no signal the ceiling is near zero and almost every pixel leaves immediately.

**Result: 26.9 ms → 5.8 ms at 3840x2160**, 52% headroom, zero late frames, and
the rendered frame identical to the last pixel (0 of 518,400 differ).

**A wrong turn worth recording.** The ceiling was first packed into the texture's
alpha channel, which held a constant 255 and looked like free space. It is not
free space. The scene graph normalises an image with alpha to a premultiplied
format on upload, scaling R, G and B by A — and R and G are the magnitude. The
spectrum display changed in **270,221 of 518,400 pixels** and every mode was
quietly wrong; only the fact that all five modes got mysteriously *faster* gave
it away. SPEC.md §Meters already said alpha was held opaque "so that nothing in
the pipeline can premultiply a data channel", which is exactly what happened. The
ceiling travels as a uniform instead. The texture's four channels are full.

### BUG-015 A path on the command line starts playing, which is not what the documents say
**Status:** fixed
**Severity:** low
**Found:** 2026-09-02, reported from use
**Related:** F-052, BUILD.md

`ferrolux ~/Music` adds the tree and immediately begins playing the first track.
BUILD.md says the opposite — "an optional file argument is loaded but not
started" — and that is what Phase 1 did: `setSource` and nothing more.

The change was made in Phase 2 and not noticed. Wiring the playlist replaced the
direct `setSource` with `playlist.setCurrentRow(0)`, and setting the current row
emits `currentEntryChanged`, which the engine answers by loading *and* playing.
Nothing in the diff looked like a behaviour change, which is how it passed
review; no test covers what the application does with an argument, because until
Phase 6 there is no CLI to test.

Handing a folder of several hundred tracks and having audio start unbidden is
also a surprising default, and F-052 hints the default was never meant to be
that: it lists `--play` among the argument forms, and an explicit `--play` earns
its place only if the bare default is something else.

**Candidate resolutions**, for the author to choose:
1. Load and select the first track without playing, matching BUILD.md and Phase
   1. Explicit `--play` then means something when F-052 arrives.
2. Keep playing, and correct BUILD.md. Matches what most players do with a file
   argument, at the cost of being startling with a directory.
3. Play for a file, select only for a directory. Matches intent most closely and
   is the most surprising to describe.

Whichever is chosen, BUILD.md must agree with it, and F-052 should record the
default alongside the flags rather than leaving it implied.

**Resolved 2026-09-02.** Candidate 1 adopted. `PlaylistModel::selectWithoutPlaying`
makes a row current and emits `currentEntryPrepared`, which the engine answers
by loading only; choosing a row by hand still emits `currentEntryChanged` and
still plays. A separate signal rather than a flag, so neither path can quietly
acquire the other's behaviour. Verified: a file argument now reaches Paused and
never Playing, and four checks in `playlist_model_test` pin both paths including
that each arms the next entry for the gapless handover.

### BUG-014 The VU and ladder pegged on ordinary music
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, reported from use
**Related:** F-032, F-033, SPEC.md §Meters, SPEC.md §Settings

Every level-driven display read close to full on normal material. The needles
sat past the end of their arc and nearly every ladder segment was lit, most of
them in the over-level colour.

Two separate causes, both scaling rather than measurement.

**The VU reference was a broadcast figure.** SPEC.md set 0 VU at −18 dBFS,
which is EBU alignment and assumes programme far quieter than a consumer
master. Measured on ordinary material: RMS runs about −13.7 dBFS with peaks near
−10.6. Against −18 that is a deflection of 1.641 rising to 2.34 — past the end
stop before the music starts. The reference is now −9 dBFS. Measured across six
ordinary tracks, sustained RMS runs −10 to −22 dBFS with loud passages reaching
−6.3; against −9 that material settles near half deflection and its loudest
moments land at 1.36, the top of the needle's travel. The arc continues past
0 VU and changes colour there, so a needle above reference reads as over rather
than as one resting against its stop. SPEC.md §Settings always
described this key as configurable and the figure as provisional; only the
default was wrong.

**The peak indicator shared the spectrum's scale.** It was normalised over the
spectrum element's −80 dB floor, so a −10 dBFS peak read 0.87 and the ladder was
almost fully lit whatever was playing. Peaks now have their own decibel scale
ending at full scale, floor −60 dB, which is what every hardware peak meter
does: −10.6 dBFS reads 0.823 and −1 dBFS reads 0.983.

Sharing one scale between the two was the underlying mistake. A sample peak
legitimately runs ten decibels or more above RMS, so any scale that suits one
pegs the other. The ladder also gained a proper per-channel peak-hold, so the
marked segment is a held maximum above the lit run rather than the run's own
top, and its over-level threshold moved to −6 dBFS so red means near clipping
rather than merely loud.

Seven checks in `tests/meters_test` now assert both scales against the measured
figures rather than against ideal ones, including that the old −18 dBFS
reference would peg — so the regression is pinned, not just corrected.


### BUG-010 The VU ballistics are specified as a system that cannot behave as described
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, starting Phase 4
**Related:** F-032, AV-012, D-005, SPEC.md §Meters

SPEC.md §Meters specifies: "The needle is a first-order system with a 300 ms
integration time to 99% of full deflection for a steady sine at reference level,
matching the IEC 60268-17 standard VU characteristic. Overshoot is 1% to 1.5%."

A first-order system's step response is monotonic. It approaches its final value
and never passes it, so it cannot overshoot by 1%, or by anything. The two halves
of that sentence describe different systems.

The IEC 60268-17 VU characteristic is a **second-order** underdamped response,
which is where the overshoot comes from and why a real VU needle visibly settles
back after a transient. That settling is a large part of what AV-012 means when
it says an instantaneous meter "reads immediately as fake".

Solved for the specified behaviour — first reaching 99% at exactly 300 ms:

| Overshoot | Damping ratio ζ | ωn (rad/s) | Peak at |
|-----------|-----------------|------------|---------|
| 1.00% | 0.8261 | 13.973 | 399 ms |
| 1.25% | 0.8127 | 13.512 | 399 ms |
| 1.50% | 0.8007 | 13.126 | 400 ms |

At the 16 ms update interval in SPEC.md §Pipeline, ωn·dt = 0.216 rad per step,
comfortably inside the stability limit for the discrete integrator.

**Recommended:** the 1.25% midpoint, ζ = 0.8127 and ωn = 13.512 rad/s, with
SPEC.md's "first-order" corrected to "second-order". The choice within the range
is genuinely open — 1% is the more conservative reading of the standard — but
first-order is not one of the options.

**Resolved 2026-09-02.** Implemented as a second-order system at the 1.25%
midpoint — ζ = 0.8127, ωn = 13.512 rad/s — integrated semi-implicitly, because
explicit Euler on an oscillator gains energy and would slowly wind the needle
up. SPEC.md §Meters now states the order correctly and carries the solved
constants. Measured by `tests/meters_test`: 99% deflection at 302.0 ms against
the 300 ms target, 1.16% overshoot peaking at 401 ms, settling at exactly 1.0000
for a reference-level signal and 0.5012 for one 6 dB below it.

The choice of 1.25% within the standard's 1% to 1.5% range stays provisional.
The order of the system does not: a first-order system cannot overshoot at all.


### BUG-011 Meter messages arrive over a second ahead of the audio
**Status:** fixed
**Severity:** high
**Found:** 2026-09-02, wiring the analysis elements in Phase 4
**Related:** F-030, F-031, F-032, AV-004, ARCHITECTURE.md §Data flow, SPEC.md §Pipeline

`level` and `spectrum` sit in `playbin3`'s audio-filter, which is upstream of
the sink. Only the sink synchronises to the clock; everything above it runs as
fast as the sink's queue will accept, so the analysis elements process — and
post — well ahead of what is being heard.

**Measured** against a reference tone through the real element arrangement:
mean lead **1307 ms**, worst **1412 ms**, over 353 samples. A meter fed on
message arrival would display a transient more than a second before it was
audible, which is not a meter.

This is not a defect in the pipeline order. SPEC.md places the analysis elements
last so the meters show the signal as heard, and that is right; the elements
report the right samples at the wrong wall-clock moment. Nor is it AV-004, which
concerns queue depth and interval jitter — this is a fixed, large offset that
jitter analysis would not reveal.

**The information needed is already present.** Every `level` and `spectrum`
message carries a `running-time`, and `gst_element_get_current_running_time` on
the pipeline gives the position actually being rendered. The values must be held
and applied when the clock reaches their timestamp, rather than on arrival.

That makes the meter path a scheduled queue rather than a direct connection, and
it needs recording in ARCHITECTURE.md §Data flow, which currently describes bus
messages as feeding `MeterSource` directly. `MeterSource` itself is unaffected —
its smoothing and ballistics are correct, they are simply being fed too early.

**Candidate resolutions:**
1. Hold timestamped frames in a queue and release them as the pipeline's running
   time advances. Exact, uses figures both elements already publish, and costs a
   bounded queue of about 1.5 seconds of frames — roughly 90 entries.
2. Move the analysis elements below the sink. Not possible with `autoaudiosink`
   without replacing it, and it would put analysis after the point where the
   signal has left the application.
3. Accept the lead. Not viable: 1.3 seconds is not a synchronisation error, it
   is a different part of the music.

Resolution 1 is recommended.

**Resolved 2026-09-02.** Candidate 1 adopted. `Engine` publishes the
`running-time` each message carries alongside the pipeline's current running
time; `MeterSource` holds frames in a bounded queue and releases them as the
clock reaches them. Every frame that has come due is applied, not just the
newest — the smoothing coefficients are defined per update interval, so skipping
frames would make the display settle faster than specified and quietly undo the
ballistics. The queue is capped at 128 frames, roughly 1.5 seconds, and its
depth is exposed so AV-004's concern can be observed rather than guessed at.
Confirmed in use: a steady depth near 150 across both queues while playing,
which is the lead being absorbed.


### BUG-012 Closing the window left the process running and the music playing
**Status:** fixed
**Severity:** high
**Found:** 2026-09-02, reported from use
**Related:** F-052, AV-001

Closing the harness window removed it from the screen but did not end the
process. Audio continued to the end of the track and then stopped, while the
process stayed alive indefinitely and had to be killed from a terminal.

**The cause was object lifetime, not the close handling.** `main()` held
`Engine` — which owns the pipeline — as a local, and called `gst_deinit()`
immediately after `app.exec()` returned. Locals are destroyed when `main`
returns, so `gst_deinit()` ran while a live pipeline still existed and blocked
for ever waiting for a teardown that could not begin until the owner was
destroyed, which could not happen until `gst_deinit()` returned.

**Fixed** by scoping every object that owns a GStreamer resource inside a block
that closes before `gst_deinit()`. Verified closing under both render loops and
mid-playback: exits in about a second in all three, against never.

**A first diagnosis was recorded here and was wrong**, which is worth keeping
rather than quietly replacing. It attributed the hang to the file and folder
choosers added for the native dialogs being windows in their own right,
defeating `quitOnLastWindowClosed`. That was plausible and false: a minimal
reproduction with an `ApplicationWindow` and a `FileDialog` quit cleanly. The
real evidence came from thread states — the main thread and the render thread
both sat in `futex_do_wait`, which said the event loop had already exited and
the process was stuck in teardown, not that quit had never fired. Instrumenting
each shutdown step then placed it exactly.

The `onClosing: Qt.quit()` handler added under the wrong diagnosis is kept. It
is not needed — `quitOnLastWindowClosed` was working the whole time — but
stating the intent explicitly costs nothing and does not depend on a heuristic
about how many windows happen to exist.

### BUG-013 The VU readout never moved because a QML binding had nothing to watch
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, reported from use
**Related:** F-030, F-032

The harness read the needle with `Meters.vuDeflection(0)`, a plain method call.
A QML binding tracks the properties it reads, and a method call exposes none, so
the expression was evaluated once at startup and never again. The readout sat at
0.000 for the whole session while the spectrum bars beside it — bound to the
`magnitudes` property, which has a change signal — moved correctly.

Two things were wrong and both had to be fixed. Deflection is now a notifying
property rather than only a method, and `advance()` emits `updated()`. It had
not: only `consumeSpectrum` did, so even a correctly bound needle would have
refreshed at the spectrum message rate rather than as the ballistics moved.

The lesson generalises past this instance: anything a shader or a delegate reads
every frame has to be a property with a change signal, not a getter.

### BUG-008 The headroom rule cancels every boost, so the equaliser can only cut
**Status:** fixed
**Severity:** high
**Found:** 2026-09-02, reported from listening
**Related:** F-020, F-021, AV-003, BUG-005, SPEC.md §Equaliser

The headroom rule attenuates by `max(0, preamp + cascade_peak)`. Since the
cascade peak *is* the largest gain the curve produces, the attenuation cancels
it exactly. The equaliser cannot make anything louder — only quieter.

| Action | Net at the peak | Net elsewhere |
|--------|-----------------|---------------|
| Preamp +6 dB, flat bands | 0.00 dB | 0.00 dB |
| Preamp +12 dB, flat bands | 0.00 dB | 0.00 dB |
| One band +12 dB | 0.00 dB | −12.00 dB |
| Built-in `treble` preset | 0.00 dB | −12.37 dB |
| One band −12 dB | 0.00 dB | 0.00 dB |

Every reported symptom follows: the preamp does nothing when raised, raising a
band makes everything else quieter in proportion, presets sound quiet, and cuts
behave normally because a cut needs no attenuation.

This is a design fault in the rule, introduced by the BUG-005 fix. That fix was
correct about *when* the output would clip and wrong to conclude the engine
should always prevent it.

**The danger AV-003 named has also changed.** Its concern was that "clipping in
a filter chain can also drive the filters into instability rather than merely
distorting". Since BUG-007 the chain runs in `F32LE`, where internal levels
above unity are ordinary and no instability follows. What remains is clipping at
the sink conversion — audible distortion, not a broken filter — which is what
every comparable player leaves to the user's preamp and volume.

**Candidate resolutions**, for the author to choose:
1. Remove the automatic attenuation. The manual preamp is the control for this,
   as it is in Winamp, foobar2000 and VLC, and AV-003's stated failure mode is
   already addressed by float processing. The worst case becomes audible
   distortion at extreme settings rather than a silently useless equaliser.
2. Make it an option, default off. Keeps the mitigation reachable for anyone who
   wants a guarantee, at the cost of a setting that needs explaining.
3. Offset it by the headroom the volume control already provides —
   `max(0, preamp + cascade + 20·log₁₀(v³))`. Mostly invisible at normal
   listening levels, but it couples equaliser behaviour to volume position,
   so moving the volume would alter the tonal balance. Surprising.

Resolution 1 is recommended. SPEC.md §Equaliser and AV-003 both need amending
to match whichever is chosen, and `tests/equaliser_test`'s worst-case check will
need restating — under resolution 1 it asserts stability and finiteness rather
than an absence of clipping.

**Resolved 2026-09-02.** Candidate 1 adopted. Nothing is subtracted from the
signal; `Equaliser::excessGain()` reports the figure so the interface can warn,
and the manual preamp is the control for level. SPEC.md §Equaliser and AV-003
are both amended. `tests/equaliser_test` now asserts that an extreme boost *does*
exceed full scale — as an equaliser should — that no sample is non-finite, and
that the reported figure bounds the measured gain: 32.04 dB measured against
35.69 dB reported, conservative by the margin expected from modelling at 192 kHz.

### BUG-009 Dragging the position bar snapped back instead of seeking
**Status:** fixed
**Severity:** medium
**Found:** 2026-09-02, reported from use
**Related:** F-003, ARCHITECTURE.md §Key invariants item 4

The harness bound the position slider's value to `Engine.position`, with a
`scrubbing` flag intended to suspend it during a drag. The flag did not work:
the expression `scrubbing ? value : Engine.position` refers to the property it
assigns, and the binding stayed live regardless, so the per-frame position poll
re-asserted the old value sixty times a second. A drag was undone as fast as it
was made.

**Fixed** with a `Binding` on the value guarded by `when: !positionBar.pressed`,
which genuinely suspends the binding while the handle is held, and
`restoreMode: Binding.RestoreNone` so releasing does not restore a stale value
before the seek lands.

Worth noting the invariant did its job here: position is polled once per frame
by design, and the defect was the UI fighting that poll rather than the poll
being wrong.

### BUG-007 The filter chain ran the equaliser in 16-bit integer, and it was audible
**Status:** fixed
**Severity:** high
**Found:** 2026-09-02, reported from listening — "the audio is very scratchy"
**Related:** F-020, AV-003, SPEC.md §Pipeline

The audio-filter bin pinned its capsfilter to `channels=2` but left the sample
format to negotiation. Against a 16-bit source the whole chain settled on
`S16LE`, so `equalizer-nbands` ran ten cascaded IIR biquads in 16-bit integer,
rounding after every section. On real music the result was plainly gritty. It
applied whether or not the equaliser was enabled, because the element processes
at unity rather than short-circuiting.

Confirmed by inspecting negotiated caps on the element's pads: `format=S16LE` on
both sink and src.

**Fixed** by adding `format=F32LE` to the same capsfilter. Caps negotiation
propagates upstream, so one constraint places the whole chain — preamp, band
filters and balance — in float, and the trailing `audioconvert` returns to
whatever the sink wants. Re-inspected: `F32LE` on both pads.

**Why the tests did not catch it.** `tests/equaliser_test` builds its own chain
around the `Equaliser` elements rather than using the engine's, so it never saw
the format the real pipeline negotiates. Worse, the bypass check passed
*honestly*: at unity gain an S16 round trip really is bit-exact, so a test
asserting transparency will pass while the element is quietly working at a
precision that ruins the signal as soon as any gain is applied. The test proved
the element was transparent and never asked what precision it was transparent
in.

`tests/acceptance_transport` now asserts, against the engine's own pipeline,
that the equaliser has negotiated a floating-point format. Checking the real
pipeline rather than a reconstruction is the point: the reconstruction is
exactly what missed this.

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
