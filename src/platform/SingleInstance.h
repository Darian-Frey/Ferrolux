// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/SingleInstance.h — one player, however many times it is launched.
//
// F-052's first clause: opening a file from the file manager while Ferrolux is
// running should put it in the playlist that is already there, not start a
// second player competing for the same audio device and the same media keys.
//
// The coordination is a D-Bus name, claimed on the session bus. That is the
// same mechanism `MprisService` and `MediaKeys` already depend on, so it adds
// no dependency and fails the same way they do — **with no session bus, every
// launch is its own player**, which is the honest behaviour for a headless or
// stripped-down session and is exactly what happened before this existed.
//
// The claim is made early, before the pipeline is built and before the window
// is loaded. A second launch has to hand over and exit without ever touching
// the audio device: two processes briefly holding the same sink is audible,
// and doing it on every file opened from a file manager would be a defect
// people would blame on the sound server.
//
// Whoever holds the name serves; everybody else hands over and leaves. There is
// no election and no negotiation, because the bus already provides both.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace ferrolux::app { class Player; }

namespace ferrolux::platform {

class SingleInstance : public QObject
{
    Q_OBJECT

    // Names the interface this object is exported under. Without it, Qt derives
    // one from the C++ class — `org.ferrolux.platform.SingleInstance` — so the
    // caller's `org.ferrolux.Ferrolux` finds the object and no interface on it,
    // and every hand-off falls through to starting a second player. The failure
    // is not silent, but it looks like a missing object rather than a naming
    // mismatch, which is not where anybody looks first.
    Q_CLASSINFO("D-Bus Interface", "org.ferrolux.Ferrolux")

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Tries to become *the* player. True means this process owns the name and
    // should carry on starting up; false means another one has it, or there is
    // no bus to ask. `available()` tells those two apart, because they need
    // different behaviour and look identical from here.
    bool claim();

    // Whether a session bus answered at all. When it did not, `claim` returns
    // true — carry on as a lone player — and `handOff` will refuse.
    bool available() const { return m_available; }

    // Asks the running instance to take these paths and raise itself. Returns
    // false if there was nobody to ask or the call failed, in which case the
    // caller should start normally rather than exiting silently: losing the
    // files is worse than a second player.
    bool handOff(const QStringList &paths, const QString &mode);

    // Publishes the object that receives those requests. Called once the player
    // and the window exist, since a request that arrives before them has
    // nowhere to go.
    void serve(app::Player *player, QObject *window);

public slots:
    // The D-Bus entry point. Named for the wire rather than for Qt style,
    // because the name is part of the interface.
    Q_SCRIPTABLE void Open(const QStringList &paths, const QString &mode);

private:
    app::Player *m_player = nullptr;
    QObject *m_window = nullptr;
    bool m_available = false;
    bool m_owner = false;
};

} // namespace ferrolux::platform
