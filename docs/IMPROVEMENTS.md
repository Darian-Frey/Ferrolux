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

*None.*

## Applied

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

### IMP-004 The `check()` test helper is duplicated across all four suites
**Status:** deferred
**Effort:** trivial
**Found:** 2026-09-02, review after Phase 3
**Related:** BUILD.md §Tests

`acceptance_transport`, `playlist_model_test`, `metadata_reader_test` and
`equaliser_test` each define their own identical `check()` and failure counter,
about fifteen lines apiece.

**Trade-offs:** A shared `tests/Check.h` removes the duplication but couples the
four suites to one header, and each is currently a single self-contained file
that can be read start to finish without following an include. Fifteen lines
repeated four times is cheap; the coupling is permanent. Worth doing only if a
fifth suite appears or the helper grows beyond printing a line.

**Deferred 2026-09-02.** Trigger: a fifth test suite. Phase 4 adds meter
ballistics tests, which will be it. Doing the extraction then means the shared
header is shaped by five real callers rather than four, and the marginal case
for it becomes an actual one.


### IMP-005 `main.cpp` owns all inter-module wiring and will not scale to Phase 6
**Status:** deferred
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

**Deferred 2026-09-02.** Trigger: Phase 6, once MPRIS2 (F-050), media keys
(F-051), single-instance enqueue (F-052) and session restore (F-015) exist.
Deferred on information rather than effort — the seams a `Player` facade should
expose are not visible until those four consumers do, and a facade designed for
guesses about them would be worse than the refactor it saves. Phase 6 should
open by building it, not by discovering it is needed.

### IMP-006 The meter shaders carry literal colours, so a theme cannot reach them
**Status:** suggested
**Effort:** small
**Noticed:** 2026-09-04, while making the meter well a `PanelSection`
**Related:** F-044, D-004, SPEC.md §Design tokens, CLAUDE.md §Conventions

`qml/MeterDisplay.qml` passes literal hexadecimal to its shaders — `barColour`
`#EF9F27`, `barColourLow` `#BA7517`, `capColour` `#F6D08A`, the flame's
`frontColour` and `backColour`, and the VU's four. The convention is explicit
that QML uses design tokens and never literal colours, and the reason is F-044:
a variant is "a token set over the same geometry", so anything holding a literal
is geometry a variant cannot reach. Under a second theme the whole panel would
change and the meters would stay exactly as they are.

Its root was the same until this commit — `color: "#2C2C2A"`, `radius: 3`, both
predating the token set — so the well the meters sit in could not be themed
either. That part is fixed; the shader properties are not.

Five of the nine are exact matches for tokens that already exist and are a
straight substitution: `barColour` and `overColour` are `readout`, `barColourLow`
and `inkColour` are `readout-dim`, `faceColour` is `display-bg`.

The other four are the problem, and the reason this is logged rather than done.
`capColour` and `needleColour` (`#F6D08A`) and the flame's two are pale, partly
desaturated tints with no token behind them, and they are not `Qt.lighter()` of
`readout` either — the nearest factor lands on `#FFB950`, which is more saturated
and visibly wrong. So they need either a palette token each, which means
extending SPEC.md's table and the check in `tests/tokens_test` that asserts it
has no extras, or a derivation that has to be tuned until it reproduces the
current appearance and verified by pixel comparison.

The flame's two in particular were chosen by eye by the author over several
iterations. Substituting a derivation for them without checking is how that work
gets quietly undone.

**Trade-offs:** Doing it costs a SPEC.md palette change — the table is currently
eight tokens and deliberately closed — or a tuning exercise against screenshots.
Leaving it means F-044 ships a theme that visibly does not apply to a fifth of
the panel, which is worse than not offering the theme. Best done *with* the
first variant rather than before it, when there is a second palette to test the
derivations against and the right answer is observable rather than guessed.
