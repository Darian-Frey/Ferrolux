// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// tests/tokens_test — the panel's appearance, checked against its specification.
//
// Three ways a token set goes quietly wrong, and one check for each.
//
//   drift     SPEC.md §Design tokens is authoritative for the `ferric` palette,
//             and a JSON file is easy to edit without opening the document it
//             answers to. The palette is asserted value by value against the
//             specified hexadecimal, so the two cannot disagree in silence.
//   gaps      qml/Tokens.qml names every token a component may use. A name it
//             asks for that the set does not have resolves to a default — black
//             for a colour, zero for a metric — and black chrome on a black
//             readout is not a visible failure. Every name is asserted present.
//   faces     a face that fails to load does not raise anything; Qt substitutes
//             a system font, and a substituted font in the middle of a lit
//             readout is a defect that has to be seen to be found. Each of the
//             four is loaded and its reported family compared with the set's.
//
// This runs without a window, so it belongs in ctest rather than in the panel
// verification pass. Loading a font needs QGuiApplication for the font database
// but not a display; the offscreen platform is enough.

#include "ui/ThemeTokens.h"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

#include <cstdio>

using ferrolux::ui::ThemeTokens;

namespace {

int failures = 0;

void check(bool ok, const char *what, const QString &detail = {})
{
    if (!ok)
        ++failures;
    std::printf("  [%s] %s%s\n", ok ? "pass" : "FAIL", what,
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — ") + detail));
}

// SPEC.md §Design tokens, transcribed. If this table and the document disagree,
// the document wins and this is the bug.
struct PaletteEntry
{
    const char *token;
    const char *value;
    const char *role;
};

constexpr PaletteEntry kFerricPalette[] = {
    { "shell",         "#B4B2A9", "chassis face" },
    { "shell-recess",  "#D3D1C7", "raised control surfaces" },
    { "shell-edge",    "#888780", "bevel and division lines" },
    { "display-bg",    "#2C2C2A", "readout background" },
    { "readout",       "#EF9F27", "primary readout amber" },
    { "readout-dim",   "#BA7517", "secondary readout amber" },
    { "readout-floor", "#854F0B", "lowest active segment, unlit ghost layer" },
    { "ink",           "#2C2C2A", "legends on the shell" },
};

// Every token qml/Tokens.qml resolves. Kept as a flat list rather than parsed
// out of the QML, because a check that derives its expectations from the thing
// it is checking cannot fail.
const QStringList kMetricTokens = {
    QStringLiteral("reference-width"), QStringLiteral("scale-min"), QStringLiteral("scale-max"),
    QStringLiteral("radius-panel"),    QStringLiteral("radius-section"),
    QStringLiteral("radius-control"),  QStringLiteral("radius-slot"),
    QStringLiteral("hairline"),        QStringLiteral("bevel"),
    QStringLiteral("bevel-light"),     QStringLiteral("bevel-shadow"),
    QStringLiteral("travel"),          QStringLiteral("travel-ms"),
    QStringLiteral("gap-panel"),       QStringLiteral("gap-section"),
    QStringLiteral("gap-control"),     QStringLiteral("pad-section"),
    QStringLiteral("pad-row"),         QStringLiteral("control-height"),
    QStringLiteral("slot-width"),      QStringLiteral("thumb-width"),
    QStringLiteral("thumb-height"),    QStringLiteral("fader-travel"),
    QStringLiteral("detent"),
};

const QStringList kTypeTokens = {
    QStringLiteral("readout-numeric"), QStringLiteral("readout-text"),
    QStringLiteral("readout-segment"), QStringLiteral("legend"),
    QStringLiteral("size-readout-large"), QStringLiteral("size-readout"),
    QStringLiteral("size-legend"), QStringLiteral("size-legend-small"),
};

// D-012 and SPEC.md §Typography: one face per type role, all four under the SIL
// Open Font License 1.1. The family names are what Qt reports after loading,
// which is not always what the file is called — Handjet ships here as a static
// instance pinned to the axis values SPEC.md specifies, and is named for them.
struct FaceEntry
{
    const char *file;
    const char *family;
    const char *role;
};

constexpr FaceEntry kFaces[] = {
    { "DSEG7Classic-Regular.ttf",        "DSEG7 Classic",                "readout-numeric" },
    { "Handjet-Panel.ttf",               "Handjet Light Circle Single", "readout-text" },
    { "DSEG14Classic-Regular.ttf",       "DSEG14 Classic",               "readout-segment" },
    { "IBMPlexSansCondensed-Regular.ttf","IBM Plex Sans Condensed",      "legend" },
};

void testPalette(const ThemeTokens &tokens)
{
    std::printf("\npalette (SPEC.md §Design tokens)\n");

    for (const PaletteEntry &entry : kFerricPalette) {
        const QColor expected(QLatin1String(entry.value));
        const QColor actual = tokens.colour(QLatin1String(entry.token));
        check(actual.isValid() && actual == expected,
              entry.token,
              QStringLiteral("%1, %2")
                  .arg(actual.isValid() ? actual.name(QColor::HexRgb).toUpper()
                                        : QStringLiteral("absent"),
                       QLatin1String(entry.role)));
    }

    // Eight-digit hexadecimal is the trap here, not a style preference. Qt reads
    // #AARRGGBB — alpha first — where most tools emit #RRGGBBAA, so a token with
    // an alpha appended becomes a different, translucent colour without error.
    // It has happened once already and turned the flame display purple.
    bool allOpaque = true;
    const QVariantMap palette = tokens.palette();
    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it)
        allOpaque = allOpaque && it.value().toString().size() == 7;
    check(allOpaque, "every colour is six-digit hexadecimal, so none can carry an alpha");

    check(palette.size() == int(std::size(kFerricPalette)),
          "the set has exactly the tokens the specification lists, and no extras",
          QStringLiteral("%1 tokens").arg(palette.size()));
}

void testMetricsAndType(const ThemeTokens &tokens)
{
    std::printf("\ntoken vocabulary (qml/Tokens.qml)\n");

    QStringList missing;
    for (const QString &token : kMetricTokens)
        if (!tokens.metrics().contains(token))
            missing.append(token);
    check(missing.isEmpty(), "every metric the panel resolves is present",
          missing.isEmpty() ? QStringLiteral("%1 tokens").arg(kMetricTokens.size())
                            : QStringLiteral("missing: %1").arg(missing.join(u", ")));

    missing.clear();
    for (const QString &token : kTypeTokens)
        if (!tokens.type().contains(token))
            missing.append(token);
    check(missing.isEmpty(), "every type token the panel resolves is present",
          missing.isEmpty() ? QStringLiteral("%1 tokens").arg(kTypeTokens.size())
                            : QStringLiteral("missing: %1").arg(missing.join(u", ")));

    // A hairline is the one metric with a stated reason to be sub-unit: F-040
    // requires it in device-independent units, and AV-005 is what a hairline
    // given in pixels does at 1.5x. Below 1.0 it resolves to one crisp physical
    // pixel at 2x rather than to two.
    check(tokens.metric(QStringLiteral("hairline")) > 0.0
              && tokens.metric(QStringLiteral("hairline")) < 1.0,
          "the hairline is sub-unit, so it stays a hairline at 2x rather than doubling",
          QStringLiteral("%1").arg(tokens.metric(QStringLiteral("hairline"))));

    check(tokens.metric(QStringLiteral("scale-min")) > 0.0
              && tokens.metric(QStringLiteral("scale-min"))
                     < tokens.metric(QStringLiteral("scale-max")),
          "the scale clamp is a range rather than a point, so resizing is continuous",
          QStringLiteral("%1 to %2")
              .arg(tokens.metric(QStringLiteral("scale-min")))
              .arg(tokens.metric(QStringLiteral("scale-max"))));

    // Travel is what makes a control read as moulded rather than drawn (F-040).
    // Zero would be a button that changes colour and does not move.
    check(tokens.metric(QStringLiteral("travel")) > 0.0
              && tokens.metric(QStringLiteral("travel-ms")) > 0.0,
          "pressed controls have travel and a duration, rather than only a colour");
}

void testFaces(const ThemeTokens &tokens, const QString &fontDir)
{
    std::printf("\nfaces (D-012, SPEC.md §Typography)\n");

    for (const FaceEntry &entry : kFaces) {
        const QString path = fontDir + QLatin1Char('/') + QLatin1String(entry.file);
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            check(false, entry.file, QStringLiteral("would not load from %1").arg(path));
            continue;
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        check(families.contains(QLatin1String(entry.family)),
              entry.file,
              QStringLiteral("reports %1, wanted %2")
                  .arg(families.join(u", "), QLatin1String(entry.family)));
    }

    // The set must ask for faces that were actually bundled. A token naming a
    // face that is not here does not fail — Qt substitutes a system font, and a
    // substitution inside a lit readout has to be noticed by eye to be found.
    QStringList bundled;
    for (const FaceEntry &entry : kFaces)
        bundled.append(QLatin1String(entry.family));

    for (const char *role : { "readout-numeric", "readout-text", "readout-segment", "legend" }) {
        const QString asked = tokens.face(QLatin1String(role));
        check(bundled.contains(asked), role,
              QStringLiteral("asks for %1").arg(asked.isEmpty() ? QStringLiteral("nothing") : asked));
    }

    // SPEC.md makes this a hard rule rather than a preference: a seven-segment
    // alphabet cannot distinguish 5 from S, 6 from b or 0 from O, so the numeric
    // face must never be the one given text. The two roles having the same face
    // would be that mistake made in the token file.
    check(tokens.face(QStringLiteral("readout-numeric"))
              != tokens.face(QStringLiteral("readout-text")),
          "the numeric and text readout roles are different faces, per the role rule");
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    const QString set = root + QStringLiteral("/resources/themes/ferric.json");

    ThemeTokens tokens;
    if (!tokens.load(set)) {
        std::printf("cannot load the token set: %s\n", qPrintable(tokens.lastError()));
        return 1;
    }
    std::printf("token set: %s\n", qPrintable(tokens.name()));

    testPalette(tokens);
    testMetricsAndType(tokens);
    testFaces(tokens, root + QStringLiteral("/resources/fonts"));

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
