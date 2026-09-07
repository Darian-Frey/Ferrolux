// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/X11MediaKeys.h — the media keys where nobody is handing them out.
// IMP-008, the case F-051 could not reach.
//
// `MediaKeys` registers with a desktop settings daemon and F-050 covers the
// desktops that route the keys through MPRIS instead. Between them that is
// GNOME, Cinnamon, MATE, KDE and anything built on those. What is left is X11
// under a bare window manager — i3, openbox, awesome — where there is no daemon
// to register with and nothing listening on MPRIS, and the keys therefore do
// nothing at all.
//
// **A global grab is a different kind of thing from a registration**, and the
// difference is why this was logged rather than written for two phases. Asking a
// daemon for the keys is a request among peers: it hands them back, it knows who
// else wanted them, and it can be told to stop. `XGrabKey` takes the key from
// every other client on the display for as long as the grab is held, and there
// is nobody to negotiate with. Held carelessly, it does not fail by not working;
// it fails by swallowing somebody's key silently.
//
// So it is held on exactly the terms the daemon registration is, and for the
// same reason (IMP-009): only while Ferrolux is the session the user means.
// `MediaKeys::evaluate` drives both, and neither knows which one it is.
//
// It is also only ever reached when no daemon answered, so on an ordinary
// desktop this code is compiled and never runs. Which is precisely why it is
// tested in a nested X server with no window manager and no daemon — the
// condition IMP-008 said to wait for, made rather than waited for.

#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>

namespace ferrolux::app { class Player; }

namespace ferrolux::platform {

class X11MediaKeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit X11MediaKeys(app::Player *player, QObject *parent = nullptr);
    ~X11MediaKeys() override;

    // Whether this build and this session can grab at all: X11 present, and Qt
    // running on it rather than on Wayland. False on Wayland by design and not
    // by omission — there is no global grab to make there, and that is the
    // compositor's decision rather than a gap to work around.
    static bool possible();

    // Takes the four transport keysyms on the root window. Returns whether any
    // were taken: on a keyboard without media keys there is nothing to grab,
    // and on a display where another client already holds them the server says
    // so, which is not this application's business to override.
    bool grab();
    void release();
    bool holding() const { return m_holding; }

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

private:
    app::Player *m_player = nullptr;

    // Keycode to the action it stands for. A keycode rather than a keysym
    // because that is what arrives in the event, and the map is rebuilt on each
    // grab so that a keyboard layout changed mid-session cannot leave it
    // pointing at the wrong keys.
    QHash<int, int> m_bound;
    bool m_holding = false;
};

} // namespace ferrolux::platform
