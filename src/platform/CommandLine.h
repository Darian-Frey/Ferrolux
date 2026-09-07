// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/CommandLine.h — what the user typed, and what it means. F-052.
//
// The three forms F-052 names are mutually exclusive and each says what should
// happen to the paths beside it. The *absence* of all three is a fourth answer
// rather than an oversight, and it is written down here because the last time
// it was merely implied it drifted to auto-play and had to be found by use —
// BUG-015. An explicit `--play` only means anything if the default is not it.
//
// The bare default is also the one form whose meaning depends on whether a
// player is already running, and the two readings in F-052's acceptance only
// look contradictory:
//
//   * "paths fill the playlist and select the first entry without starting
//     playback" — a fresh start, where the list is empty and filling it and
//     appending to it are the same act.
//   * "a second launch with file arguments enqueues into the running instance"
//     — where selecting the first arrival would yank the cursor away from
//     whatever is playing, which is not what enqueueing means.
//
// So the bare default appends in both cases and only selects when there was
// nothing there to disturb. `SingleInstance` decides which of the two applies,
// because it is the part that knows whether anyone else is running.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "app/Player.h"

namespace ferrolux::platform {

class CommandLine
{
public:
    struct Result {
        QList<QUrl> paths;

        // What was asked for. `Player::AddAndSelect` is the bare default and is
        // the one `SingleInstance` may downgrade to `AddOnly` when handing over
        // to a player that is already running.
        app::Player::Open mode = app::Player::AddAndSelect;

        // Set when the user asked for `--help` or `--version`; the parser has
        // already printed it and the caller should exit without starting.
        bool handled = false;
    };

    // Parses `arguments` in the form `QCoreApplication::arguments()` returns.
    // Taken rather than fetched, so the mapping from flags to modes can be
    // exercised without a process per case: `arguments()` is fixed when the
    // application is constructed, and only one of those exists at a time.
    //
    // Paths become absolute here, on the machine that typed them, because a
    // relative path handed to a player running in another working directory
    // means a different file or none at all.
    static Result parse(const QStringList &arguments);

    // The wire forms of `Player::Open`, for handing a request to an instance
    // that is already running. Words rather than the enumerator's number, so
    // that a change to the enumeration cannot silently reinterpret a message.
    static QString name(app::Player::Open mode);
    static app::Player::Open mode(const QString &name);
};

} // namespace ferrolux::platform
