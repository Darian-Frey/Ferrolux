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

    return ferrolux::tests::summary();
}
