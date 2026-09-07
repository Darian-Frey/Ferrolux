// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The parts of `platform/` that need nothing but the build. IMP-010.
//
// Most of that module cannot be a unit test and should not pretend to be: an
// MPRIS service needs a session bus, media keys need a settings daemon or an X
// server, and a single-instance hand-off needs two processes. Those are
// `tools/verify-desktop.sh`, for the same reason AV-002 and AV-005 are tools —
// a check that cannot run on the build machine is not a test, it is a script
// that fails.
//
// What is left is small and is the part most likely to break without anybody
// noticing. `CommandLine`'s mapping between `Player::Open` and its wire words
// is a **protocol between two processes**: a second launch sends the word and
// the running player acts on it. If the two halves stop agreeing, nothing
// crashes and nothing logs — a `--replace` from a file manager quietly becomes
// the bare default and appends where it was told to replace, and the user's
// playlist is wrong in a way they will blame on themselves.
//
// The default is checked hardest, because BUG-015 is what happens when it
// drifts: an unrecognised word must mean the form that does not play.

#include "Check.h"

#include "platform/CommandLine.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringList>

#include <cstdio>

using ferrolux::app::Player;
using ferrolux::platform::CommandLine;
using ferrolux::tests::check;

namespace {

QString modeName(Player::Open mode)
{
    switch (mode) {
    case Player::AddAndSelect:   return QStringLiteral("AddAndSelect");
    case Player::AddOnly:        return QStringLiteral("AddOnly");
    case Player::AddAndPlay:     return QStringLiteral("AddAndPlay");
    case Player::ReplaceAndPlay: return QStringLiteral("ReplaceAndPlay");
    }
    return QStringLiteral("?");
}

// Every mode there is. Listed rather than iterated over a range, so adding one
// to the enumeration without adding it here is a compile error in `modeName`
// rather than a case that quietly goes unchecked.
const Player::Open kAllModes[] = {
    Player::AddAndSelect, Player::AddOnly, Player::AddAndPlay, Player::ReplaceAndPlay,
};

void testWireNames()
{
    std::printf("CommandLine — the wire protocol between two launches\n");

    for (Player::Open mode : kAllModes) {
        const QString wire = CommandLine::name(mode);
        check(!wire.isEmpty(), "every mode has a wire name",
              QStringLiteral("%1 -> %2").arg(modeName(mode), wire));
        check(CommandLine::mode(wire) == mode, "and it round-trips back",
              QStringLiteral("%1 -> %2 -> %3")
                  .arg(modeName(mode), wire, modeName(CommandLine::mode(wire))));
    }

    // Distinct, or two modes collapse into one over the wire and the collision
    // is invisible from either side.
    QSet<QString> seen;
    for (Player::Open mode : kAllModes)
        seen.insert(CommandLine::name(mode));
    check(seen.size() == int(std::size(kAllModes)),
          "no two modes share a wire name",
          QStringLiteral("%1 names for %2 modes").arg(seen.size()).arg(std::size(kAllModes)));

    // The words themselves, spelled out. A rename is a protocol break between
    // an old running instance and a new one launched over it, and it should
    // fail here rather than in somebody's playlist.
    check(CommandLine::name(Player::AddOnly) == QLatin1String("enqueue"),
          "AddOnly is `enqueue` on the wire");
    check(CommandLine::name(Player::AddAndPlay) == QLatin1String("play"),
          "AddAndPlay is `play`");
    check(CommandLine::name(Player::ReplaceAndPlay) == QLatin1String("replace"),
          "ReplaceAndPlay is `replace`");
    check(CommandLine::name(Player::AddAndSelect) == QLatin1String("select"),
          "AddAndSelect is `select`");
}

void testUnknownWordIsSafe()
{
    std::printf("\nThe default, which BUG-015 is about\n");

    // Anything unrecognised must be the form that does not play. A newer
    // instance sending a word an older one has never heard must not start
    // playing music at somebody.
    for (const char *nonsense : { "", "PLAY", "Replace", "resume", "enqueue2", "../play" }) {
        const QString word = QString::fromLatin1(nonsense);
        check(CommandLine::mode(word) == Player::AddAndSelect,
              "an unrecognised word is the silent default",
              QStringLiteral("\"%1\" -> %2").arg(word, modeName(CommandLine::mode(word))));
    }
}

void testFlags()
{
    std::printf("\nFlags to modes\n");

    const struct { QStringList argv; Player::Open expected; const char *what; } cases[] = {
        { { "ferrolux", "a.mp3" },              Player::AddAndSelect,
          "no flag adds and selects, and does not play" },
        { { "ferrolux", "--enqueue", "a.mp3" }, Player::AddOnly,        "--enqueue" },
        { { "ferrolux", "--play", "a.mp3" },    Player::AddAndPlay,     "--play" },
        { { "ferrolux", "--replace", "a.mp3" }, Player::ReplaceAndPlay, "--replace" },
        // Later wins rather than being an error: somebody who writes both meant
        // the more decisive of the two, and refusing to start over an argument
        // is a worse answer than doing the more specific thing.
        { { "ferrolux", "--enqueue", "--replace", "a.mp3" }, Player::ReplaceAndPlay,
          "the more decisive form wins when two are given" },
    };

    for (const auto &c : cases) {
        const auto result = CommandLine::parse(c.argv);
        check(result.mode == c.expected, c.what,
              QStringLiteral("%1 -> %2").arg(c.argv.mid(1).join(QLatin1Char(' ')),
                                             modeName(result.mode)));
    }

    // Paths are made absolute against this process, because a relative path
    // handed to a player running elsewhere names a different file or none.
    const auto relative = CommandLine::parse({ QStringLiteral("ferrolux"),
                                               QStringLiteral("track.mp3") });
    check(relative.paths.size() == 1, "a path becomes one entry");
    check(!relative.paths.isEmpty() && relative.paths.first().isLocalFile()
              && relative.paths.first().toLocalFile().startsWith(QLatin1Char('/')),
          "and it is resolved to an absolute file URL",
          relative.paths.isEmpty() ? QString() : relative.paths.first().toString());

    // A launcher that hands over a URL rather than a path. `%F` in the desktop
    // entry is specified to pass local paths, but not every launcher honours
    // that, and the whole URL treated as a relative filename would name
    // something that does not exist — silently.
    const auto url = CommandLine::parse({ QStringLiteral("ferrolux"),
                                          QStringLiteral("file:///tmp/track.mp3") });
    check(url.paths.size() == 1 && url.paths.first().toLocalFile()
              == QLatin1String("/tmp/track.mp3"),
          "a file: URL is taken as a URL, not as a filename",
          url.paths.isEmpty() ? QString() : url.paths.first().toLocalFile());

    const auto none = CommandLine::parse({ QStringLiteral("ferrolux") });
    check(none.paths.isEmpty() && none.mode == Player::AddAndSelect,
          "no arguments is no paths and the silent default");
}

} // namespace

int main(int argc, char *argv[])
{
    // `parse` needs no application, but `QCommandLineParser` uses the
    // application name in its messages, so one exists for tidiness rather than
    // necessity. Its own argv is not what is parsed — every case passes its own.
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ferrolux"));

    testWireNames();
    testUnknownWordIsSafe();
    testFlags();

    return ferrolux::tests::summary();
}
