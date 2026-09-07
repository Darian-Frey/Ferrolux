// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Holds SPEC.md to the code, and the code to SPEC.md. IMP-007.
//
// The settings table lists every key the application persists. It listed three
// that nothing read or wrote for most of Phase 6, and nothing in the table
// distinguished them from the twenty-four that worked — a reader had to grep
// the source to find out which half of the document they were in. The count
// went from three to one when F-015 landed, and nothing anywhere said so.
//
// This checks both directions, which is the point. An unmarked key must be
// implemented, so the table cannot promise a setting the application does not
// have. A key marked **Planned** must *not* be implemented, so the marker
// cannot quietly stop being true once somebody builds the thing. A one-way
// check would rot in the direction nobody was watching.
//
// It would have caught BUG-019, where `equaliser/preset` was written on exit
// and never read on start: the key was in the table, in the saving code, and
// absent from the restoring code, and it took a lit field on the panel to make
// anyone notice.
//
// **This reads a document, and that is a real cost.** Reflowing the settings
// table breaks this test, and the failure will look like a code problem when it
// is a formatting one — so the messages below name SPEC.md §Settings by name
// and say what shape they expected. The alternative was a status column
// maintained by hand, which is the same information with nothing checking it.

#include "Check.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <iterator>

#include <cstdio>

using ferrolux::tests::check;

namespace {

struct Row {
    QString key;
    bool planned = false;
};

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

// The rows of the one table in §Settings. Recognised by its header rather than
// by position, so adding a section above it does not silently empty this test —
// which would leave every check passing over nothing at all.
QList<Row> settingsTable(const QString &spec, bool *found)
{
    QList<Row> rows;
    *found = false;

    const QStringList lines = spec.split(QLatin1Char('\n'));
    int at = lines.indexOf(QStringLiteral("| Key | Type | Default | Notes |"));
    if (at < 0)
        return rows;
    *found = true;

    // Past the header and its separator, then every consecutive table line.
    for (int i = at + 2; i < lines.size() && lines.at(i).startsWith(QLatin1Char('|')); ++i) {
        const QStringList cells = lines.at(i).split(QLatin1Char('|'));
        if (cells.size() < 3)
            continue;

        static const QRegularExpression quoted(QStringLiteral("`([^`]+)`"));
        const auto m = quoted.match(cells.at(1));
        if (!m.hasMatch())
            continue;

        Row row;
        row.key = m.captured(1);
        row.planned = lines.at(i).contains(QStringLiteral("**Planned.**"));
        rows.append(row);
    }
    return rows;
}

// Every settings key written as a literal anywhere under src/. Keys are only
// ever spelled out — `QStringLiteral("meters/mode")` — and a key assembled by
// concatenation would be invisible here; that is a limit of this check and the
// reason to keep spelling them out.
QSet<QString> keysInCode(const QString &root, const QSet<QString> &groups)
{
    QSet<QString> found;
    QDirIterator it(root + QStringLiteral("/src"), { QStringLiteral("*.cpp"),
                                                     QStringLiteral("*.h") },
                    QDir::Files, QDirIterator::Subdirectories);

    // Anchored on the groups the table itself declares, so unrelated
    // slash-bearing strings in the source — GStreamer's `audio/x-raw`, a MIME
    // type, a D-Bus path — cannot be mistaken for settings keys.
    static const QRegularExpression literal(QStringLiteral("\"([a-z]+)/([A-Za-z0-9/_<>-]+)\""));
    while (it.hasNext()) {
        const QString text = readAll(it.next());
        auto matches = literal.globalMatch(text);
        while (matches.hasNext()) {
            const auto m = matches.next();
            if (groups.contains(m.captured(1)))
                found.insert(m.captured(1) + QLatin1Char('/') + m.captured(2));
        }
    }
    return found;
}

// `equaliser/user/<name>` is a family rather than a key: one entry per user
// preset, the name being the key. The code spells the family three ways — the
// group `equaliser/user` for iterating, and `equaliser/user/` joined to a name
// for reading and writing one — so the match is on the stem, with or without
// the separator. Requiring the trailing slash meant the group form read as an
// undocumented key, which is a test failing on a difference that does not
// exist.
bool implemented(const QString &key, const QSet<QString> &code)
{
    if (!key.endsWith(QStringLiteral("<name>")))
        return code.contains(key);

    QString stem = key.left(key.indexOf(QStringLiteral("<name>")));
    while (stem.endsWith(QLatin1Char('/')))
        stem.chop(1);

    for (const QString &used : code) {
        if (used == stem || used.startsWith(stem + QLatin1Char('/')))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

    std::printf("spec_test — SPEC.md against the code\n\n");
    std::printf("§Settings\n");

    const QString spec = readAll(root + QStringLiteral("/docs/SPEC.md"));
    check(!spec.isEmpty(), "SPEC.md is readable",
          spec.isEmpty() ? QStringLiteral("looked in %1/docs").arg(root) : QString());
    if (spec.isEmpty())
        return ferrolux::tests::summary();

    bool foundTable = false;
    const QList<Row> rows = settingsTable(spec, &foundTable);
    check(foundTable, "the settings table is where it says it is",
          foundTable ? QString()
                     : QStringLiteral("no row `| Key | Type | Default | Notes |` in §Settings"));
    check(rows.size() > 20, "the table has its rows",
          QStringLiteral("%1 found").arg(rows.size()));
    if (!foundTable)
        return ferrolux::tests::summary();

    QSet<QString> groups;
    for (const Row &row : rows)
        groups.insert(row.key.left(row.key.indexOf(QLatin1Char('/'))));

    const QSet<QString> code = keysInCode(root, groups);
    check(!code.isEmpty(), "settings keys are found in the source",
          QStringLiteral("%1 distinct").arg(code.size()));

    // ---- every unmarked key is implemented ---------------------------------
    QStringList promised;
    for (const Row &row : rows) {
        if (!row.planned && !implemented(row.key, code))
            promised.append(row.key);
    }
    check(promised.isEmpty(),
          "every key the table states is one the application reads or writes",
          promised.isEmpty() ? QString()
                             : QStringLiteral("documented but absent: %1 — mark the row "
                                              "**Planned.** or implement it")
                                   .arg(promised.join(QStringLiteral(", "))));

    // ---- every marked key is not ------------------------------------------
    QStringList stale;
    for (const Row &row : rows) {
        if (row.planned && implemented(row.key, code))
            stale.append(row.key);
    }
    check(stale.isEmpty(),
          "no key is still marked Planned after being implemented",
          stale.isEmpty() ? QString()
                          : QStringLiteral("implemented but marked planned: %1 — drop the "
                                           "marker").arg(stale.join(QStringLiteral(", "))));

    // ---- and nothing is used that the table never mentioned -----------------
    QStringList undocumented;
    for (const QString &used : code) {
        bool listed = false;
        for (const Row &row : rows) {
            if (row.key == used || implemented(row.key, { used })) {
                listed = true;
                break;
            }
        }
        if (!listed)
            undocumented.append(used);
    }
    undocumented.sort();
    check(undocumented.isEmpty(),
          "no key is written that SPEC.md §Settings does not describe",
          undocumented.isEmpty() ? QString()
                                 : QStringLiteral("in the code, not in the table: %1")
                                       .arg(undocumented.join(QStringLiteral(", "))));

    // ---- the desktop entry says what MPRIS says ----------------------------
    // The list of media types the application opens is stated twice: once in
    // `resources/ferrolux.desktop`, which is what puts it in a file manager's
    // "Open with", and once in `MprisRoot::supportedMimeTypes`, which is what a
    // shell asks. They are the same claim, and nothing but this makes them
    // agree — a type added to one is silently absent from the other.
    std::printf("\nThe desktop entry against MPRIS\n");

    const QString entry = readAll(root + QStringLiteral("/resources/ferrolux.desktop"));
    check(!entry.isEmpty(), "the desktop entry is readable");

    const QString mpris = readAll(root + QStringLiteral("/src/platform/MprisService.cpp"));
    if (!entry.isEmpty() && !mpris.isEmpty()) {
        QSet<QString> declared;
        for (const QString &line : entry.split(QLatin1Char('\n'))) {
            if (!line.startsWith(QStringLiteral("MimeType=")))
                continue;
            for (const QString &type : line.mid(9).split(QLatin1Char(';'), Qt::SkipEmptyParts))
                declared.insert(type.trimmed());
        }

        QSet<QString> advertised;
        static const QRegularExpression audio(QStringLiteral("\"(audio/[A-Za-z0-9.+-]+)\""));
        auto it = audio.globalMatch(mpris);
        while (it.hasNext())
            advertised.insert(it.next().captured(1));

        check(!declared.isEmpty(), "the entry declares media types",
              QStringLiteral("%1 of them").arg(declared.size()));
        check(declared == advertised,
              "and they are exactly the ones MPRIS advertises",
              declared == advertised
                  ? QStringLiteral("%1 types").arg(declared.size())
                  : QStringLiteral("only in the entry: {%1}; only in MPRIS: {%2}")
                        .arg(QStringList((declared - advertised).values()).join(QStringLiteral(", ")),
                             QStringList((advertised - declared).values()).join(QStringLiteral(", "))));
    }

    // The three places that name the desktop file must agree, or a shell finds
    // the icon through one of them and nothing through the others.
    const QString main = readAll(root + QStringLiteral("/src/main.cpp"));
    check(main.contains(QStringLiteral("setDesktopFileName(QStringLiteral(\"ferrolux\"))")),
          "main.cpp names the desktop file `ferrolux`");
    check(mpris.contains(QStringLiteral("QString MprisRoot::desktopEntry"))
              && mpris.contains(QStringLiteral("return QStringLiteral(\"ferrolux\")")),
          "and MPRIS reports the same name");

    // ---- AV-001 ------------------------------------------------------------
    // ARCHITECTURE.md §Key invariants item 1: the streaming thread does no
    // application work. This is the static half of AV-001's detection; the
    // measured half is `core/StreamTimer` and `tools/stress-audio.sh`.
    //
    // It reads the source because there is nothing else to read. A streaming
    // thread is not a type, GStreamer does not mark which of its callbacks run
    // on one, and the compiler cannot tell an allocation on the audio path from
    // an allocation anywhere else. What can be checked mechanically is which
    // callbacks exist, that each one is accounted for, and what the ones on the
    // audio path are written out of.
    std::printf("\nAV-001, the streaming thread (ARCHITECTURE.md §Key invariants 1)\n");
    {
        // Every GStreamer callback the project registers, wherever it lives.
        QStringList registrations;
        QDirIterator sources(root + QStringLiteral("/src"),
                             QStringList() << QStringLiteral("*.cpp") << QStringLiteral("*.h"),
                             QDir::Files, QDirIterator::Subdirectories);
        QString engineSource;
        while (sources.hasNext()) {
            const QString path = sources.next();
            const QString text = readAll(path);
            if (path.endsWith(QStringLiteral("core/Engine.cpp")))
                engineSource = text;

            static const QRegularExpression connect(
                QStringLiteral("g_signal_connect[^(]*\\([^,]+,\\s*\"([a-z0-9-]+)\""));
            auto it = connect.globalMatch(text);
            while (it.hasNext())
                registrations << it.next().captured(1);
        }

        // The inventory. A signal named here has been classified; anything else
        // is a callback nobody has decided the thread of, which is exactly the
        // state AV-001 says is expensive to discover later.
        //
        // `about-to-finish` is emitted from the streaming thread as the current
        // stream runs out. It is the only one, and it is the reason this vector
        // is Critical rather than theoretical.
        static const QSet<QString> streamingThread = { QStringLiteral("about-to-finish") };
        static const QSet<QString> mainLoop = {};

        const QSet<QString> found(registrations.begin(), registrations.end());
        const QSet<QString> unclassified = found - streamingThread - mainLoop;
        check(unclassified.isEmpty(),
              "every GStreamer signal the project connects is classified by thread",
              unclassified.isEmpty()
                  ? QStringLiteral("%1 connected, %2 on a streaming thread")
                        .arg(found.size()).arg((found & streamingThread).size())
                  : QStringLiteral("unclassified: {%1} — add it to spec_test's inventory "
                                   "and say which thread it runs on")
                        .arg(QStringList(unclassified.values()).join(QStringLiteral(", "))));

        check(found.contains(QStringLiteral("about-to-finish")),
              "and the one known to run on a streaming thread is still connected",
              QStringLiteral("otherwise this section is checking nothing"));

        // A sync handler runs on whichever thread posted the message, which for
        // anything the audio path posts is a streaming thread. It is the single
        // most likely way this invariant gets broken, because it is what the
        // documentation reaches for when a bus message needs to be seen sooner.
        bool syncHandler = false;
        QDirIterator again(root + QStringLiteral("/src"),
                           QStringList() << QStringLiteral("*.cpp") << QStringLiteral("*.h"),
                           QDir::Files, QDirIterator::Subdirectories);
        while (again.hasNext())
            if (readAll(again.next()).contains(QStringLiteral("gst_bus_set_sync_handler")))
                syncHandler = true;
        check(!syncHandler,
              "the bus is read by a watch on the main loop, not by a sync handler");

        // The body of the streaming-thread callback, and what it may not
        // contain. Brace-matched from the signature rather than read to the end
        // of the file, so that adding a function below it does not silently
        // widen what is being checked.
        const int signature = engineSource.indexOf(QStringLiteral("void aboutToFinish(GstElement"));
        const int open = engineSource.indexOf(QLatin1Char('{'), signature);
        int depth = 0;
        int close = open;
        for (; close < engineSource.size(); ++close) {
            if (engineSource.at(close) == QLatin1Char('{'))
                ++depth;
            else if (engineSource.at(close) == QLatin1Char('}') && --depth == 0)
                break;
        }
        const QString body = signature >= 0 && close > open
            ? engineSource.mid(open, close - open)
            : QString();
        check(!body.isEmpty() && body.contains(QStringLiteral("g_object_set")),
              "the streaming-thread callback's body was located",
              QStringLiteral("%1 characters").arg(body.size()));

        check(body.contains(QStringLiteral("StreamScope")),
              "it is timed, so AV-001's measured half has something to measure");

        // Each of these is a way to stall an audio thread, and each reads as
        // ordinary code everywhere else in the project — which is the whole
        // difficulty with this invariant. Logging allocates and takes a lock
        // inside Qt; a signal emission can be direct-connected and run arbitrary
        // slots on this thread; a blocking invoke waits on a main loop that may
        // be busy laying out a playlist of 20,000 rows.
        struct Forbidden { const char *token; const char *why; };
        static const Forbidden forbidden[] = {
            { "emit ",                  "emits a Qt signal" },
            { "Q_EMIT",                 "emits a Qt signal" },
            { "QMetaObject::invokeMethod", "calls across threads" },
            { "BlockingQueuedConnection", "blocks on the main loop" },
            { "qDebug",                 "logs" },
            { "qInfo",                  "logs" },
            { "qWarning",               "logs" },
            { "qCritical",              "logs" },
            { "qCDebug",                "logs" },
            { "qCWarning",              "logs" },
            { "printf",                 "does I/O" },
            { "new ",                   "allocates" },
            { "malloc",                 "allocates" },
            { "QString",                "allocates" },
            { "QFile",                  "does file I/O" },
            { "gst_element_set_state",  "changes pipeline state from inside it" },
            { "gst_element_query",      "queries the pipeline, which can block" },
        };
        QStringList violations;
        for (const Forbidden &f : forbidden)
            if (body.contains(QLatin1String(f.token)))
                violations << QStringLiteral("%1 (%2)")
                                  .arg(QLatin1String(f.token), QLatin1String(f.why));
        check(violations.isEmpty(),
              "and does no application work: no allocation, no logging, no signal, no blocking call",
              violations.isEmpty()
                  ? QStringLiteral("%1 constructs checked for").arg(int(std::size(forbidden)))
                  : violations.join(QStringLiteral("; ")));
    }

    return ferrolux::tests::summary();
}
