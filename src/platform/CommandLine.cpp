// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace ferrolux::platform {

using app::Player;

CommandLine::Result CommandLine::parse(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("CommandLine",
            "Ferrolux RS-1 — a cassette futurism audio player.\n\n"
            "With no option, paths are added to the playlist and the first is\n"
            "selected, but nothing starts playing. A second launch adds to the\n"
            "player already running rather than starting another."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption enqueue(QStringLiteral("enqueue"),
        QCoreApplication::translate("CommandLine",
            "Add to the end of the playlist, leaving playback alone."));
    const QCommandLineOption play(QStringLiteral("play"),
        QCoreApplication::translate("CommandLine",
            "Add to the playlist and start playing the first of them."));
    const QCommandLineOption replace(QStringLiteral("replace"),
        QCoreApplication::translate("CommandLine",
            "Clear the playlist, add these, and start playing."));
    parser.addOption(enqueue);
    parser.addOption(play);
    parser.addOption(replace);
    parser.addPositionalArgument(QStringLiteral("path"),
        QCoreApplication::translate("CommandLine", "Files or folders to add."),
        QStringLiteral("[path...]"));

    Result result;

    // `process` exits the application itself for --help, --version and for a
    // malformed option, which is why nothing below has to handle those.
    parser.process(arguments);

    // Later forms win rather than being an error. Someone who writes both meant
    // the more decisive of the two, and refusing to start over an argument is a
    // worse answer than doing the more specific thing.
    if (parser.isSet(enqueue))
        result.mode = Player::AddOnly;
    if (parser.isSet(play))
        result.mode = Player::AddAndPlay;
    if (parser.isSet(replace))
        result.mode = Player::ReplaceAndPlay;

    // Resolved here, against this process's working directory. A relative path
    // handed to a player running somewhere else names a different file or none
    // at all, and the failure is silent: the path simply does not exist and the
    // track never appears.
    for (const QString &argument : parser.positionalArguments())
        result.paths.append(QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath()));

    return result;
}

QString CommandLine::name(Player::Open mode)
{
    switch (mode) {
    case Player::AddOnly:        return QStringLiteral("enqueue");
    case Player::AddAndPlay:     return QStringLiteral("play");
    case Player::ReplaceAndPlay: return QStringLiteral("replace");
    case Player::AddAndSelect:   break;
    }
    return QStringLiteral("select");
}

Player::Open CommandLine::mode(const QString &name)
{
    if (name == QLatin1String("enqueue"))
        return Player::AddOnly;
    if (name == QLatin1String("play"))
        return Player::AddAndPlay;
    if (name == QLatin1String("replace"))
        return Player::ReplaceAndPlay;

    // Anything unrecognised is the bare default, which is the one that cannot
    // surprise: it adds, and it does not make a sound.
    return Player::AddAndSelect;
}

} // namespace ferrolux::platform
