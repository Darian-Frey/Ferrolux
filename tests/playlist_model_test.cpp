// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 2 playlist tests. Covers F-010 through F-012 and the scale clause of
// the ROADMAP.md Phase 2 acceptance criterion. No files are touched: play order
// is pure logic over URLs, and testing it without I/O is the whole reason
// ARCHITECTURE.md puts play order in the model rather than in the engine.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>

#include "library/PlaylistFilter.h"
#include "library/PlaylistIO.h"
#include "library/PlaylistModel.h"

using ferrolux::library::MetadataState;
using ferrolux::library::PlaylistEntry;
using ferrolux::library::PlaylistFilter;
using ferrolux::library::PlaylistModel;
namespace PlaylistIO = ferrolux::library::PlaylistIO;

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

void testPlaylistIO()
{
    std::printf("\nplaylist file I/O (F-013)\n");

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check(false, "temporary directory available");
        return;
    }
    QDir(dir.path()).mkpath(QStringLiteral("music"));

    QList<PlaylistEntry> entries;
    for (int i = 0; i < 3; ++i) {
        PlaylistEntry entry;
        entry.url = QUrl::fromLocalFile(QDir(dir.path()).absoluteFilePath(
            QStringLiteral("music/track-%1.flac").arg(i)));
        entry.title = QStringLiteral("Track %1").arg(i);
        entry.artist = QStringLiteral("Kraftwerk");
        entry.durationNs = qint64(120 + i) * 1'000'000'000LL;
        entries.append(entry);
    }
    // One entry outside the playlist's tree, which must be written absolute.
    PlaylistEntry outside;
    outside.url = QUrl::fromLocalFile(QStringLiteral("/elsewhere/outside.flac"));
    outside.title = QStringLiteral("Outside");
    outside.durationNs = -1;
    entries.append(outside);

    for (const char *name : { "list.m3u", "list.m3u8", "list.pls" }) {
        const QString path = QDir(dir.path()).absoluteFilePath(QLatin1String(name));
        QString error;
        const bool saved = PlaylistIO::save(path, entries, &error);
        check(saved, (QStringLiteral("%1 saves").arg(QLatin1String(name))).toUtf8().constData(), error);

        const QList<PlaylistEntry> loaded = PlaylistIO::load(path, &error);
        check(loaded.size() == entries.size(),
              (QStringLiteral("%1 round-trips every entry").arg(QLatin1String(name))).toUtf8().constData(),
              QStringLiteral("%1 of %2").arg(loaded.size()).arg(entries.size()));

        bool urlsMatch = loaded.size() == entries.size();
        bool durationsMatch = urlsMatch;
        for (int i = 0; urlsMatch && i < loaded.size(); ++i) {
            urlsMatch = urlsMatch && loaded.at(i).url == entries.at(i).url;
            durationsMatch = durationsMatch && loaded.at(i).durationNs == entries.at(i).durationNs;
        }
        check(urlsMatch, (QStringLiteral("%1 round-trips URLs exactly").arg(QLatin1String(name))).toUtf8().constData());
        check(durationsMatch, (QStringLiteral("%1 round-trips durations, -1 included").arg(QLatin1String(name))).toUtf8().constData());
    }

    // SPEC.md: relative where the playlist sits at or above the media, absolute
    // otherwise. A reference that would have to climb out is written absolute.
    QFile m3u(QDir(dir.path()).absoluteFilePath(QStringLiteral("list.m3u")));
    m3u.open(QIODevice::ReadOnly);
    const QString written = QString::fromUtf8(m3u.readAll());
    check(written.contains(QStringLiteral("music/track-0.flac"))
              && !written.contains(QStringLiteral("../")),
          "media below the playlist is referenced relatively");
    check(written.contains(QStringLiteral("/elsewhere/outside.flac")),
          "media outside the playlist's tree is referenced absolutely");
    check(written.startsWith(QStringLiteral("#EXTM3U")), "M3U is written with an #EXTM3U header");

    // Read tolerantly: no header, unknown directives, CRLF, blank lines, and a
    // stray comment. None of these is an error.
    const QString messyPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("messy.m3u"));
    QFile messy(messyPath);
    messy.open(QIODevice::WriteOnly);
    messy.write("\r\n# just a comment\r\n#EXTVLCOPT:something\r\n"
                "#EXTINF:99,Artist - A Song\r\nmusic/track-0.flac\r\n"
                "\r\nmusic/track-1.flac\r\n#EXTGRP:unknown\r\n");
    messy.close();
    const QList<PlaylistEntry> messyLoaded = PlaylistIO::load(messyPath);
    check(messyLoaded.size() == 2, "unknown directives and blank lines are skipped, not fatal",
          QStringLiteral("%1 entries").arg(messyLoaded.size()));
    check(messyLoaded.value(0).durationNs == 99'000'000'000LL,
          "#EXTINF duration is read in seconds");
    check(messyLoaded.value(1).durationNs == -1,
          "an entry with no #EXTINF gets an unknown duration");

    // PLS with gaps and out-of-order keys.
    const QString plsPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("odd.pls"));
    QFile pls(plsPath);
    pls.open(QIODevice::WriteOnly);
    pls.write("[playlist]\nTitle2=Second\nFile2=music/track-1.flac\nLength2=-1\n"
              "File1=music/track-0.flac\nNumberOfEntries=2\nVersion=2\n"
              "Unknown7=ignored\n");
    pls.close();
    const QList<PlaylistEntry> plsLoaded = PlaylistIO::load(plsPath);
    check(plsLoaded.size() == 2, "PLS entries are collected regardless of key order",
          QStringLiteral("%1 entries").arg(plsLoaded.size()));
    check(plsLoaded.value(0).url.fileName() == QStringLiteral("track-0.flac"),
          "PLS entries are ordered by index, not by line");
}

void testFilter()
{
    std::printf("\nlive filter (F-014)\n");

    PlaylistModel model;
    model.addUrls(synthesise(1000));

    QList<PlaylistEntry> tags;
    tags.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        PlaylistEntry entry;
        entry.url = model.entryAt(i).url;
        entry.title = QStringLiteral("Radioactivity %1").arg(i);
        entry.artist = (i % 2) ? QStringLiteral("Kraftwerk") : QStringLiteral("Neu!");
        entry.album = QStringLiteral("Radio-Activity");
        entry.metadata = MetadataState::Loaded;
        tags.append(entry);
    }
    model.applyMetadata(tags);

    PlaylistFilter filter;
    filter.setSourceModel(&model);
    check(filter.rowCount() == 1000, "an empty filter shows every row",
          QString::number(filter.rowCount()));

    // The clause that matters: filtering is a view concern and must leave play
    // order exactly as it was.
    model.setCurrentRow(500);
    const int nextBefore = model.nextRow();
    filter.setFilterText(QStringLiteral("Kraftwerk"));
    check(filter.rowCount() == 500, "filter narrows the visible rows",
          QString::number(filter.rowCount()));
    check(model.rowCount() == 1000, "filtering does not remove rows from the model");
    check(model.nextRow() == nextBefore, "filtering does not alter play order");
    check(model.currentRow() == 500, "filtering does not move the current entry");

    // Every term must match somewhere, so word order does not matter.
    filter.setFilterText(QStringLiteral("kraftwerk radio-activity"));
    check(filter.rowCount() == 500, "all terms must match, in any order",
          QString::number(filter.rowCount()));

    filter.setFilterText(QStringLiteral("kraftwerk nonsense"));
    check(filter.rowCount() == 0, "a term that matches nothing empties the view");

    filter.setFilterText(QString());
    check(filter.rowCount() == 1000, "clearing the filter restores every row");

    // Row mapping, which selection and playback depend on.
    filter.setFilterText(QStringLiteral("Neu!"));
    const int source = filter.toSourceRow(0);
    check(source >= 0 && filter.fromSourceRow(source) == 0,
          "proxy and source rows map back to each other");
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

    // A filter runs on every keystroke, so its cost is felt directly.
    PlaylistFilter filter;
    filter.setSourceModel(&model);
    timer.restart();
    filter.setFilterText(QStringLiteral("track-01"));
    const qint64 filterMs = timer.elapsed();
    check(filterMs < 250, "filtering 20,000 rows stays inside a keystroke",
          QStringLiteral("%1 ms, %2 rows visible").arg(filterMs).arg(filter.rowCount()));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testContents();
    testPlayOrder();
    testSortAndCurrent();
    testPlaylistIO();
    testFilter();
    testScale();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
