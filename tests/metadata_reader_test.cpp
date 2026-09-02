// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 2 metadata tests. Exercises the worker pool against real files,
// including the two failure states that matter: a path that does not resolve
// (AV-010) and a file that resolves but is not audio (AV-009).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QUrl>

#include <cstdio>

#include "library/MetadataReader.h"

using namespace ferrolux::library;

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

const char *stateName(MetadataState state)
{
    switch (state) {
    case MetadataState::Pending: return "Pending";
    case MetadataState::Loaded:  return "Loaded";
    case MetadataState::Failed:  return "Failed";
    case MetadataState::Missing: return "Missing";
    }
    return "?";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList files = app.arguments().mid(1);
    if (files.size() < 2) {
        std::fprintf(stderr, "usage: metadata_reader_test <flac> <vbr-mp3>\n");
        return 2;
    }

    MetadataReader reader;
    QList<PlaylistEntry> collected;
    QObject::connect(&reader, &MetadataReader::batchReady,
                     [&collected](const QList<PlaylistEntry> &batch) { collected += batch; });

    const QUrl flac = QUrl::fromLocalFile(QFileInfo(files.at(0)).absoluteFilePath());
    const QUrl mp3 = QUrl::fromLocalFile(QFileInfo(files.at(1)).absoluteFilePath());
    const QUrl absent = QUrl::fromLocalFile(QStringLiteral("/nonexistent/ferrolux-no-such-file.flac"));
    const QUrl notAudio = QUrl::fromLocalFile(QStringLiteral("/etc/hostname"));

    std::printf("\nmetadata reading\n");
    reader.enqueue({ flac, mp3, absent, notAudio });

    QElapsedTimer timer;
    timer.start();
    while (collected.size() < 4 && timer.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    check(collected.size() == 4, "every enqueued URL produced a result",
          QStringLiteral("%1 of 4").arg(collected.size()));

    const auto find = [&collected](const QUrl &url) -> PlaylistEntry {
        for (const PlaylistEntry &entry : collected)
            if (entry.url == url)
                return entry;
        return {};
    };

    const PlaylistEntry flacResult = find(flac);
    check(flacResult.metadata == MetadataState::Loaded, "FLAC reads as Loaded",
          stateName(flacResult.metadata));
    check(flacResult.durationNs > 30'000'000'000LL && flacResult.durationNs < 35'000'000'000LL,
          "FLAC duration is plausible",
          QStringLiteral("%1 ms").arg(flacResult.durationNs / 1'000'000));
    check(flacResult.fileDate.isValid(), "file date populated");

    const PlaylistEntry mp3Result = find(mp3);
    check(mp3Result.metadata == MetadataState::Loaded, "VBR MP3 reads as Loaded",
          stateName(mp3Result.metadata));
    // A VBR MP3 carrying a Xing header is exact. One without is extrapolated from
    // its first frame and can be well out — real files of both kinds exist, so
    // the guarantee is "positive and present", not "correct". SPEC.md §Duration
    // makes the engine authoritative once the track actually plays.
    check(mp3Result.durationNs > 0, "VBR MP3 reports a duration",
          QStringLiteral("%1 ms").arg(mp3Result.durationNs / 1'000'000));
    if (files.size() > 2) {
        const QUrl xing = QUrl::fromLocalFile(QFileInfo(files.at(2)).absoluteFilePath());
        reader.enqueue({ xing });
        timer.restart();
        while (collected.size() < 5 && timer.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const PlaylistEntry xingResult = find(xing);
        check(xingResult.durationNs > 32'000'000'000LL && xingResult.durationNs < 33'000'000'000LL,
              "a VBR MP3 with a Xing header is read accurately",
              QStringLiteral("%1 ms").arg(xingResult.durationNs / 1'000'000));
    }

    check(find(absent).metadata == MetadataState::Missing,
          "a path that does not resolve reports Missing (AV-010)",
          stateName(find(absent).metadata));
    check(find(notAudio).metadata == MetadataState::Failed,
          "a non-audio file reports Failed (AV-009)",
          stateName(find(notAudio).metadata));

    // A row must be displayable before its tags arrive.
    PlaylistEntry pending;
    pending.url = QUrl::fromLocalFile(QStringLiteral("/music/Some Album/03 - Airwaves.flac"));
    check(pending.displayTitle() == QStringLiteral("03 - Airwaves"),
          "an untagged row falls back to the file's base name",
          pending.displayTitle());

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
