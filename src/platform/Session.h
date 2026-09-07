// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/Session.h — the listening session, across a restart. F-015.
//
// Four things, and they are not one thing four times: the playlist contents,
// the play order, which track was current, and how far into it playback had
// reached. Each is stored where it belongs rather than all of them in one
// place — the contents in a playlist file, because that is what a playlist file
// is for and it stays readable by anything else; the rest in SPEC.md §Settings,
// because they are three numbers.
//
// **The play order is the part that is easy to get wrong.** F-012 requires that
// shuffle be a permutation the model holds and does not recompute on each
// advance. Restoring `playback/shuffle` and letting the model reshuffle would
// satisfy every visible symptom — shuffle is on, the list is in a random
// order — while performing exactly the recomputation F-012 forbids, once, at
// launch. The upcoming tracks would differ from the ones that were coming up
// when the user quit, and nobody would ever be able to say why. So the
// permutation is saved and adopted whole.
//
// Nothing here starts playback. A player that resumes on launch because it was
// playing when it closed is a player that makes noise in a quiet room, and
// BUG-015 is the entry recording that the bare launch does not play. The
// position is restored so that pressing Play continues where it left off, and
// that is as far as it goes.

#pragma once

#include <QObject>
#include <QString>

namespace ferrolux::app { class Player; }

namespace ferrolux::platform {

class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(app::Player *player, QObject *parent = nullptr);

    // Loads the saved playlist, adopts its order and selects the track that was
    // current, without playing. Returns false when there was nothing to restore,
    // which is the ordinary first run rather than a fault.
    bool restore();

    // Seeks to where playback had reached. Separate from `restore` because the
    // caller has to decide: paths on the command line mean the user asked for a
    // different track, and resuming into a position that belongs to a track
    // they did not ask for is worse than starting it at zero.
    void resume();

public slots:
    // Connect once to `aboutToQuit`, beside `Settings::save`.
    void save();

private:
    static QString playlistPath();

    app::Player *m_player = nullptr;
    qint64 m_position = 0;
};

} // namespace ferrolux::platform
