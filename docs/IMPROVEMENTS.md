# Improvements

Catalogue of code-quality improvements, refactors, and architectural
changes proposed during development. Per the project workflow,
improvements are **logged here when noticed, not silently applied**
(see Maintenance Rule 8). The author decides whether to apply, defer,
or decline.

This is the dual of BUGS.md: bugs are things that are broken,
improvements are things that work but could be better.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

Every entry requires a `Trade-offs:` field. An entry without one is a
feature request, not an improvement candidate, and should be rejected
at review time.

See DECISIONS.md D-011 for why this catalogue lives in the repository.

---

## Suggested

### IMP-010 `platform/` has four classes and no automated coverage
**Status:** suggested
**Effort:** medium
**Found:** 2026-09-06, finishing F-052
**Related:** F-015, F-050, F-051, F-052, IMP-004

`Settings`, `MprisService`, `MediaKeys`, `SingleInstance` and `CommandLine` are
between them most of Phase 6, and not one of them is touched by any of the six
suites. Every one has been verified by hand — a bus client, synthesised keysyms,
a settings round trip, four launch forms — and none of those checks will run
again unless somebody remembers to run them.

Some of it genuinely cannot be a unit test: a D-Bus service needs a session bus,
and media keys need a settings daemon. That is the same argument that made
`measure-frames.sh` and `verify-scaling.sh` tools rather than tests, and the
same answer would serve — a script that starts the player, drives it over the
bus, and asserts. The hand checks already written are most of such a script.

But part of it needs no bus at all and is the part most likely to break
silently. `CommandLine::name` and `CommandLine::mode` are a wire protocol
between two processes: if they stop round-tripping, a `--replace` from a file
manager silently becomes the bare default and the user's playlist is appended to
instead of replaced. That is a pure function over four values and wants nothing
but a test.

**Trade-offs:** The pure part is cheap but pulls `app/Player.h` and therefore
GStreamer's headers into a suite that needs neither, which is either an awkward
link or a reason to move the mapping somewhere lighter — and moving it to suit a
test is the tail wagging the dog. The scripted part costs a session bus in CI,
which the project does not have and may not want; a check that cannot run on the
build machine joins AV-002 and AV-005 as a thing people forget. Against both:
`platform/` is the layer whose failures are least visible from inside the
application, because every one of them looks like the desktop being odd.

### IMP-008 Media keys do nothing under a bare window manager
**Status:** suggested
**Effort:** medium
**Found:** 2026-09-06, implementing F-051
**Related:** F-051, F-050, AV-005

`platform/MediaKeys` registers with a desktop settings daemon, and F-050 covers
the desktops that route media keys through MPRIS instead. Between them that is
GNOME, Cinnamon, MATE, KDE and anything built on them, on X11 and Wayland
alike. What is left over is X11 with a bare window manager — i3, openbox,
awesome and their like — where there is no daemon to register with and nothing
listening on MPRIS, so the keys do nothing at all.

The fix is the `XGrabKey` that F-051's note originally assumed, on the four
`XF86Audio` keysyms, repeated across the modifier combinations that Num Lock and
Scroll Lock produce, with an event filter to match. It is not written, and the
reason is worth stating: a global grab takes a key away from every other
application on the machine, and this one could not be tested here — the session
that would exercise it is exactly the session this was not developed on. A grab
that is wrong in a way nobody noticed does not lose a feature, it eats somebody's
keyboard.

Wayland with no daemon cannot be fixed at all: there is no global grab to make,
by design. That case is MPRIS or nothing, and it is the compositor's decision.

**Trade-offs:** A global grab is the only mechanism available, and it takes the
key from every other application on the machine for as long as Ferrolux runs —
including from a player that would have handled it better. It cannot be made
polite the way the daemon registration can, because there is nobody to hand the
key back to and no way to know who wanted it. Against that, the case it fixes
is a real one: on a bare window manager the keys currently do nothing at all.
The deciding factor is that the fix cannot be tested on any machine available
here, and an untested global grab fails by swallowing a key silently rather
than by not working.

**Worth doing when** there is a bare-WM session to test in, or a user reports it.
Not before: the failure it prevents is invisible on the machines available, and
the failure it could introduce is not.

### IMP-007 SPEC.md's settings table does not say which keys exist
**Status:** suggested
**Effort:** small
**Found:** 2026-09-06, gathering the keys into `platform/Settings`
**Related:** F-015, F-042, SPEC.md §Settings

The table lists twenty-seven keys with a type, a default and a note. Three of
them are not read or written by any code in the tree — `ui/geometry`,
`session/playlist` and `session/position` — and nothing in the table
distinguishes them from the twenty-four that are.

Two of the three are honestly pending: `session/playlist` and `session/position`
are F-015, which is still to come in this phase. `ui/geometry` belongs to no
feature entry at all, which is why it is worth recording rather than assuming
somebody has it in hand — a documented setting that nothing implements and
nothing is committed to implementing is a promise the file is making on the
application's behalf.

**Trade-offs:** A status column adds a field that has to be kept true, and a
document that lies about its own status column is worse than one that says
nothing — SPEC.md currently claims nothing, which is at least honest. A test
that walks the table cannot be fooled that way, but it couples the document's
formatting to a test, so reflowing a table breaks a build. The cheaper half is
that both fixes are small; the real cost of neither is that a reader takes the
table as a description of the program when three of its rows are a plan.

Gathering the keys into one class is what made this visible: every string in
SPEC.md §Settings is now in `platform/Settings` except those three, so the gap
is a diff rather than a memory. It also makes the table checkable, which is the
suggestion — a status column, or a check that walks the table and asserts each
key appears in `Settings`. The second would have caught BUG-019, where a key was
written on exit and never read on start.

## Applied

### IMP-009 Ferrolux keeps the media keys even when it is not the player being used
**Status:** applied
**Effort:** small
**Found:** 2026-09-06, reported by the author from watching the F-051 testing
**Related:** F-051, F-050

`MediaKeys::attach()` is called once at startup and the registration is renewed
only when the settings daemon restarts. The daemon gives the media keys to
whichever application registered *last*, so launching Ferrolux takes them from
whatever had them and does not hand them back until Ferrolux exits — including
when Ferrolux is idle with nothing loaded and the other application is the one
actually playing. Press play expecting the video in the browser and the silent
music player answers instead.

The convention this misses is that a media player re-registers when its window
is activated, so the most recently *used* player holds the keys rather than the
most recently *started* one. That is why the daemon takes a timestamp at all —
the ordering is meant to be maintained, not established once.

**The fix is small**: re-register on window activation, which means `MediaKeys`
needs the window it already refuses to know about. `MprisService` and `Settings`
both take one as a plain `QObject`, so the shape exists; a connection to the
window's `activeChanged` calling `attach()` again is most of it. Worth deciding
whether an idle player should re-register at all, or only one that has something
loaded — the second is friendlier and is a condition rather than a signal.

**Trade-offs:** Holding the keys unconditionally is simpler and has one
advantage worth naming — the keys always reach Ferrolux while it runs, so they
never appear to break. Claiming on activity means there are moments when a
press does not reach the player, and a user who does not know the rule will read
that as unreliable rather than as polite. The rule therefore has to be one that
can be stated in a sentence, or it is worse than the blunt version. Against
that: taking a key from an application that is actively using it is a fault the
user notices immediately and cannot diagnose, which is exactly how this was
found.

Not applied inline when first logged, per Maintenance Rule 8: it is a change to
how the application behaves towards other applications, which is the author's
call rather than a defect to be quietly corrected.

**Applied 2026-09-06**, with the author choosing that a paused Ferrolux should
keep the keys: pause is the state a player is left in when the user means to
come back, so Play should resume the music rather than something else. In
practice a browser re-registers the moment its own video starts, so this only
decides who wins when nothing else is playing.

The keys are now claimed on activity rather than on existence. `MediaKeys` takes
the window — read by property name, as `Settings` and `MprisService` already
do — and re-evaluates on three things: the engine's state, the current row, and
the window's focus.

**Reading the engine's state turned out to be insufficient, in a way that put
the original defect straight back.** A track loaded from the command line sits
at `Engine::Paused` on row 0 having never made a sound, which is the same state
as a session paused halfway through. A rule written as "playing, or paused with
a track" therefore claimed the keys at launch — the exact behaviour this entry
exists to stop. It was caught by instrumenting the decision rather than by
reasoning about it. What is remembered instead is whether playback has actually
happened since the last stop.

Verified by watching `GrabMediaPlayerKeys` and `ReleaseMediaPlayerKeys` on the
bus rather than by pressing keys, so the test could not disturb whatever else
was playing — which is how the defect was found in the first place. The
sequence: grab on launch while focused; **release** when focus moves away and
nothing has played; grab when playback starts even though still unfocused; keep
through a pause; **release** on stop.


### IMP-005 `main.cpp` owns all inter-module wiring and will not scale to Phase 6
**Status:** applied
**Effort:** medium
**Found:** 2026-09-02, review after Phase 3
**Related:** F-015, F-050, F-051, F-052, ARCHITECTURE.md §Module responsibilities

Every connection between the engine, the playlist and the metadata reader is a
lambda in `main()`. At present that is a virtue — the whole control flow is
readable on one screen, which is why it was written that way. Phase 6 adds
MPRIS2, media keys, single-instance enqueue and session restore, each of which
needs to observe and command the same objects, and the function will stop being
readable well before all four land.

A `Player` facade owning the wiring is the obvious shape, and ARCHITECTURE.md
does not currently name one.

**Trade-offs:** Introducing it now adds an indirection layer to something that
does not yet need one, and the right seams are not visible until the Phase 6
features exist to shape them — building the facade early risks designing it for
the wrong four consumers. Leaving it means Phase 6 begins with a refactor
instead of a feature. The decision is when, not whether.

**The trigger has arrived: Phase 6 is next.** `main()` has also grown since this
was written — it now restores and saves the theme, the compact state and the
inverted display alongside everything it already did. The question this entry
poses is due now rather than deferred, and the honest answer is probably that
the first Phase 6 feature should be the facade rather than MPRIS2.

**Deferred 2026-09-02.** Trigger: Phase 6, once MPRIS2 (F-050), media keys
(F-051), single-instance enqueue (F-052) and session restore (F-015) exist.
Deferred on information rather than effort — the seams a `Player` facade should
expose are not visible until those four consumers do, and a facade designed for
guesses about them would be worse than the refactor it saves. Phase 6 should
open by building it, not by discovering it is needed.

**Applied 2026-09-06**, as Phase 6's opening move rather than after the four
consumers existed. Its own text disagreed with itself on that — the deferral
condition said the trigger was Phase 6 *once* MPRIS2, media keys, enqueue and
session restore existed, while the paragraph above it said Phase 6 should open
by building the facade. The second reading was taken, on the grounds that all
four consumers already have written acceptance criteria in FEATURES.md, so the
seams could be shaped against a specification rather than against guesses; the
alternative was wiring four desktop services into `main()` and then unpicking
them.

`src/app/Player` owns the engine, the playlist, the metadata reader, the meter
source and the filter view, holds all fifteen connections between them, and
offers the transport surface MPRIS2 and the media keys both need. `main()` drops
from 378 lines to 307 and no longer contains a single `connect`.

**`app/` is a new module, and the reason it is not `core/`** is that `core/`,
`library/`, `meters/` and `ui/` include nothing from one another — verified, not
assumed — and each is testable alone because of it. Putting the facade in
`core/` would have made `core/` depend on `library/` and `meters/` and turned
"core owns the pipeline" into "core owns everything". `app/` is instead declared
to be the one module allowed to know about several peers, which is exactly what
`main()` was doing unnamed. `platform/` will call into it rather than into
`core/`, so the desktop code couples one way.

The facade adds no policy. Play order still belongs to `PlaylistModel` and the
three-second rule on `previous` still belongs to `Engine`; `Player::previous()`
calls the engine and lets the engine's own signal route back to the model,
rather than deciding anything itself. Its one piece of judgement is
`Player::Open` — what should happen to paths arriving from outside — because
that is the one question neither of the others can answer alone. Which
command-line flag selects which mode is left to F-052 and deliberately not
encoded in the names.

Verified by driving the panel under XTest, since no suite covers the wiring and
the wiring is the whole of what moved: a directory loads paused at 1 of 217,
play populates the stream format, next advances, previous after three seconds
restarts the current track rather than going back — F-002's rule intact through
the facade — pause holds and stop resets to zero. 321 checks still pass in Debug
and Release.

### IMP-006 The meter shaders carry literal colours, so a theme cannot reach them
**Status:** applied
**Effort:** small
**Noticed:** 2026-09-04, while making the meter well a `PanelSection`
**Applied:** 2026-09-05
**Related:** F-044, D-004, SPEC.md §Design tokens, CLAUDE.md §Conventions

`qml/MeterDisplay.qml` passed literal hexadecimal to its shaders. The convention
is explicit that QML uses design tokens and never literal colours, and the reason
is F-044: a variant is "a token set over the same geometry", so anything holding a
literal is geometry a variant cannot reach. Under a set that changed the lamp the
whole panel would have changed and the meters would have stayed amber.

Six were exact matches for existing tokens and were substituted directly.

**The other five turned out not to need tokens at all**, which is why this closed
without touching SPEC.md's palette. Measured in HSL against `readout`, every one
of them is the same lamp at a different hue and lightness:

| role | offset from the lamp |
|------|----------------------|
| cap, VU needle | +3°, same saturation, +0.21 lightness |
| flame far rank | +4.5°, ×1.16 saturation, +0.23 lightness |
| flame near rank | −11°, ×0.91 saturation, +0.05 lightness |
| over-reference segment | −22.6°, ×0.92 saturation, +0.02 lightness |
| unlit segment | the well with 7.5% of the lamp bled into it |

That is what a physical display does — one phosphor, driven harder or softer —
so they are derived in `qml/Tokens.qml` rather than written down. A set that
changes the lamp now gets its cap, its flame and its warning tier for free, and
no set has to specify five values that are a function of one it already has.

Fitted rather than guessed, and verified twice. Against the literals they
replace, the worst channel error over all five modes is **3/255**, which is below
what the eye resolves — the flame's two in particular were tuned by eye by the
author over several passes, and the point of fitting was to keep that work. And
with the lamp temporarily set to VFD green, the peak tier moved from 39.3° to
145.0° while the lamp moved from 36° to 142°: the offset is preserved, so the
derivation follows rather than coincides.

**Trade-offs:** A variant can no longer tune the flame independently of the lamp.
That is the intended constraint rather than a cost — the flame *is* the lamp, and
a set that could disagree with itself about what colour its display is would be a
set that eventually does. Where a future finish genuinely needs a second lamp
colour — a two-colour ladder with real red over-segments, say — that is a palette
token and a SPEC.md change, and it should be argued on its own rather than
smuggled in as a default.

### IMP-003 `PlaylistModel::moveSelection` is quadratic in the selection size
**Status:** applied
**Effort:** small
**Found:** 2026-09-02, measured during review after Phase 3
**Related:** F-011, AV-008

The method tests `rows.contains(row)` inside two passes over the whole list, so
cost grows as rows × selected. Measured on a 20,000-entry playlist moving a
10,000-row selection: **790 ms in a Debug build, 66 ms in Release.** The Debug
figure sits just inside the one-second bound the test asserts and would exceed
it on slower hardware or a larger list.

A `QSet<int>` or a `QBitArray` mask over the row range makes both passes linear.

**Trade-offs:** A mask costs one allocation of `rowCount()` bits per move, which
is nothing, but it adds a second representation of the selection that has to
stay in step with the sorted list already being used for ordering. The current
form is obviously correct on inspection, and a ten-thousand-row drag is not a
gesture anyone performs by accident. This is a real cost that may never be paid.

**Applied 2026-09-02.** A `QBitArray` over the row range replaces the membership
test against the sorted list, making both passes linear. Measured on the same
20,000-entry, 10,000-row-selection case:

| Build | Before | After |
|-------|--------|-------|
| Debug | 790 ms | **8 ms** |
| Release | 66 ms | **1 ms** |

The benchmark that motivated the entry is a permanent check, so the improvement
cannot silently regress.


### IMP-001 `MetadataReader` carries dead state and never signals completion
**Status:** applied
**Effort:** small
**Found:** 2026-09-02, review after Phase 3
**Related:** F-010, F-015, AV-008

`m_outstanding` is incremented for every batch enqueued and reset by `cancel()`,
but never decremented when a batch finishes. `idle()` is declared and emitted
only from `cancel()`, so it fires when work is abandoned and never when work is
done. Nothing consumes either, so nothing is currently wrong — but the state
reads as if completion were tracked, and the next person to want "tell me when
the playlist has finished populating" will believe it already works.

That signal has real uses coming: session restore (F-015) should not save a
playlist mid-read, and sorting by duration is meaningless until durations exist.

**Trade-offs:** Removing both is trivial and honest, but discards a hook that
Phase 6 will want and that costs little to keep. Implementing it properly means
each batch posting a completion back to the owner thread — one extra queued
invocation per 64 files, negligible against the tag reading itself, but it adds
a second cross-thread path to reason about where there is currently one.

**Applied 2026-09-02.** Each batch now carries its completion note in the same
queued invocation as its results, so a delivered batch and its bookkeeping
cannot be observed out of order, and a batch cancelled after doing its work
skips both. `idle()` fires once when every enqueued batch has reported, and
`progressChanged(completed, total)` is added for the progress reporting a large
import wants. Cancelling resets the counters rather than decrementing them by
work that will never report. Four checks cover it, including that a second run
reports once rather than firing immediately on stale counters.


## Declined

### IMP-002 `Engine::poll()` re-queries duration on every rendered frame
**Status:** declined
**Effort:** trivial
**Found:** 2026-09-02, review after Phase 3
**Related:** F-003, AV-002, ARCHITECTURE.md §Key invariants item 4

Position genuinely changes every frame; duration almost never does. `poll()`
issues both queries unconditionally, so a duration query runs sixty times a
second to return the same answer. `GST_MESSAGE_DURATION_CHANGED` already resets
the cache to −1, so the query could run only while `m_duration < 0`.

**Trade-offs:** The saving is small — a duration query is not expensive — and
the current code is robust by brute force: if an element ever fails to post
`DURATION_CHANGED` when the duration becomes known, polling still discovers it
on the next frame, whereas the conditional version would leave the row reading
`--:--` forever. Trading a self-healing property for a minor saving is only
worth it if the query proves to cost something measurable, which it has not.

**Declined 2026-09-02.** The saving is real but unmeasured and almost certainly
negligible; the property being traded away is not. Polling unconditionally means
that if any element ever fails to post `DURATION_CHANGED` when a duration
becomes known, the next frame discovers it anyway. The conditional version would
leave that row reading `--:--` for the life of the track, and the failure would
be intermittent and format-specific — the most expensive kind to diagnose.

Recorded as declined rather than left suggested so that the next reader finds
the reasoning instead of re-proposing it. Reopen only if duration polling shows
up in a profile, which would require it to cost something it currently does not.


## Deferred

### IMP-004 The `check()` test helper is duplicated across every suite
**Status:** deferred
**Effort:** trivial
**Found:** 2026-09-02, review after Phase 3
**Related:** BUILD.md §Tests

`acceptance_transport`, `playlist_model_test`, `metadata_reader_test`,
`equaliser_test`, `meters_test` and `tokens_test` each define their own identical
`check()` and failure counter, about fifteen lines apiece. It was four suites
when this was written.

**Trade-offs:** A shared `tests/Check.h` removes the duplication but couples
every suite to one header, and each is currently a single self-contained file
that can be read start to finish without following an include. Fifteen lines
repeated a few times is cheap; the coupling is permanent. Worth doing only if
more suites appear or the helper grows beyond printing a line.

**Deferred 2026-09-02.** Trigger: a fifth test suite.

**The trigger has fired, twice.** `meters_test` arrived in Phase 4 and
`tokens_test` in Phase 5, so the helper is now copied six times rather than
four, and the shared header would be shaped by six real callers. The condition
this was deferred against no longer holds; whether to act on that is still the
author's, but it should be decided rather than left to drift — the count only
goes up.


