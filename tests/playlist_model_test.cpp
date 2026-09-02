// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 2 playlist tests. Covers F-010 through F-012 and the scale clause of
// the ROADMAP.md Phase 2 acceptance criterion. No files are touched: play order
// is pure logic over URLs, and testing it without I/O is the whole reason
// ARCHITECTURE.md puts play order in the model rather than in the engine.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QUrl>

#include <cstdio>

#include "library/PlaylistModel.h"

using ferrolux::library::PlaylistModel;

namespace {

int failures = 0;

void check(bool ok, const char *what, const QString &detail = {})
{
    std::printf("  [%s] %s%s%s\n", ok ? "pass" : "FAIL", what,
                detail.isEmpty() ? "" : " — ", detail.isEmpty() ? "" : qPrintable(detail));
    std::fflush(stdout);
    if (!ok)
        ++failures;
}

QList<QUrl> synthesise(int count, const QString &prefix = QStringLiteral("track"))
{
    QList<QUrl> urls;
    urls.reserve(count);
    for (int i = 0; i < count; ++i)
        urls.append(QUrl(QStringLiteral("file:///music/%1-%2.flac")
                             .arg(prefix).arg(i, 6, 10, QLatin1Char('0'))));
    return urls;
}

// Walks a full pass and reports how many entries were visited and how many were
// visited more than once.
struct Pass { int visits = 0; int distinct = 0; };

Pass walk(PlaylistModel &model, int guard)
{
    Pass pass;
    QSet<int> seen;
    while (model.advance()) {
        ++pass.visits;
        seen.insert(model.currentRow());
        if (pass.visits > guard)
            break;
    }
    pass.distinct = int(seen.size());
    return pass;
}

void testContents()
{
    std::printf("\ncontents and editing\n");
    PlaylistModel model;

    model.addUrls(synthesise(10));
    check(model.rowCount() == 10, "adds rows");

    model.removeRows({ 2, 3, 4 });
    check(model.rowCount() == 7, "removes a contiguous selection");

    model.removeRows({ 0, 6 });
    check(model.rowCount() == 5, "removes a discontiguous selection");

    check(model.canUndo(), "undo is available after a removal");
    model.undo();
    check(model.rowCount() == 7, "undo restores the last removal only");
    check(!model.canUndo(), "undo is single-level and does not stack");

    model.clear();
    check(model.rowCount() == 0, "clears");
    model.undo();
    check(model.rowCount() == 7, "clear is undoable");

    // F-011 drag reorder.
    PlaylistModel ordered;
    ordered.addUrls(synthesise(5, QStringLiteral("m")));
    const QUrl third = ordered.entryAt(2).url;
    ordered.moveRows(2, 1, 0);
    check(ordered.entryAt(0).url == third, "moveRows relocates a single row");
    check(ordered.rowCount() == 5, "moveRows preserves the row count");
}

void testPlayOrder()
{
    std::printf("\nplay order (F-012)\n");

    // Sequential, repeat off: one pass, in order, stopping at the end.
    PlaylistModel sequential;
    sequential.addUrls(synthesise(50));
    const Pass plain = walk(sequential, 200);
    check(plain.visits == 50, "sequential pass visits every entry", QString::number(plain.visits));
    check(!sequential.advance(), "repeat-off stops at the end of the list");

    // Shuffle: a permutation, not independent random picks. This is the clause
    // that separates a correct implementation from the naive one.
    PlaylistModel shuffled;
    shuffled.addUrls(synthesise(200));
    shuffled.setShuffle(true);
    const Pass pass = walk(shuffled, 500);
    check(pass.visits == 200, "shuffled pass makes exactly one visit per entry",
          QString::number(pass.visits));
    check(pass.distinct == 200, "shuffled pass has no repeats before exhaustion",
          QStringLiteral("%1 distinct of %2").arg(pass.distinct).arg(pass.visits));

    // The permutation must be held, not re-rolled. Asking twice must answer the
    // same, or shuffle has degenerated into a dice roll per advance.
    PlaylistModel stable;
    stable.addUrls(synthesise(100));
    stable.setShuffle(true);
    stable.advance();
    const int first = stable.nextRow();
    check(first == stable.nextRow() && first == stable.nextRow(),
          "nextRow is stable — the permutation is not recomputed per advance");

    // Repeat-all wraps, and a shuffled list earns a fresh permutation for the
    // next pass rather than replaying the same one.
    PlaylistModel repeating;
    repeating.addUrls(synthesise(100));
    repeating.setShuffle(true);
    repeating.setRepeat(PlaylistModel::RepeatAll);
    QSet<int> secondPass;
    int steps = 0;
    while (steps < 200 && repeating.advance()) {
        if (steps >= 100)
            secondPass.insert(repeating.currentRow());
        ++steps;
    }
    check(steps == 200, "repeat-all keeps advancing past the end", QString::number(steps));
    check(secondPass.size() == 100, "the second pass is itself a complete permutation",
          QString::number(secondPass.size()));

    // Repeat-one holds position.
    PlaylistModel single;
    single.addUrls(synthesise(10));
    single.setCurrentRow(4);
    single.setRepeat(PlaylistModel::RepeatOne);
    single.advance();
    single.advance();
    check(single.currentRow() == 4, "repeat-one stays on the same entry");

    // Toggling shuffle must not lose the playing track.
    PlaylistModel toggling;
    toggling.addUrls(synthesise(100));
    toggling.setCurrentRow(42);
    toggling.setShuffle(true);
    check(toggling.currentRow() == 42, "enabling shuffle preserves the current entry");
    toggling.setShuffle(false);
    check(toggling.currentRow() == 42, "disabling shuffle preserves the current entry");
}

void testSortAndCurrent()
{
    std::printf("\nsorting and current-entry tracking\n");

    PlaylistModel model;
    model.addUrls(synthesise(100));
    model.setCurrentRow(10);
    const QUrl playing = model.entryAt(model.currentRow()).url;

    model.sortBy(PlaylistModel::ByPath, Qt::DescendingOrder);
    check(model.entryAt(0).url.toString() > model.entryAt(99).url.toString(),
          "descending sort orders the rows");
    check(model.currentRow() >= 0 && model.entryAt(model.currentRow()).url == playing,
          "sorting follows the current entry to its new row");

    model.sortBy(PlaylistModel::ByPath, Qt::AscendingOrder);
    check(model.entryAt(0).url.toString() < model.entryAt(99).url.toString(),
          "ascending sort orders the rows");
    check(model.entryAt(model.currentRow()).url == playing,
          "re-sorting still follows the current entry");

    // Removing other rows must not move the playing entry.
    model.removeRows({ 0, 1, 2 });
    check(model.entryAt(model.currentRow()).url == playing,
          "removing other rows keeps the current entry current");
}

void testScale()
{
    std::printf("\nscale (AV-008)\n");
    constexpr int kEntries = 20000;

    PlaylistModel model;
    QElapsedTimer timer;

    timer.start();
    model.addUrls(synthesise(kEntries));
    const qint64 addMs = timer.elapsed();
    check(model.rowCount() == kEntries, "20,000 entries added");
    check(addMs < 1000, "add completes under a second", QStringLiteral("%1 ms").arg(addMs));

    timer.restart();
    model.sortBy(PlaylistModel::ByTitle, Qt::AscendingOrder);
    const qint64 sortMs = timer.elapsed();
    check(sortMs < 1000, "sort completes under a second", QStringLiteral("%1 ms").arg(sortMs));

    timer.restart();
    model.setShuffle(true);
    const qint64 shuffleMs = timer.elapsed();
    check(shuffleMs < 1000, "shuffle completes under a second", QStringLiteral("%1 ms").arg(shuffleMs));

    timer.restart();
    model.removeRows({ 0, 1, 2, 3, 4 });
    const qint64 removeMs = timer.elapsed();
    check(removeMs < 1000, "removal from a large list is not quadratic",
          QStringLiteral("%1 ms").arg(removeMs));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testContents();
    testPlayOrder();
    testSortAndCurrent();
    testScale();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
