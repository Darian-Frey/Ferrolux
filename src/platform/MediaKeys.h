// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/MediaKeys.h — the keys above the number row. F-051.
//
// F-051's note said this would be delegated to MPRIS under Wayland and want a
// global grab under X11. Half of that is right and the half that is wrong is
// the important half, so it is recorded here rather than quietly worked around:
// **the major desktops do not route media keys to MPRIS at all.** GNOME,
// Cinnamon and MATE run a settings daemon that owns the keys and hands them to
// whichever application last *registered* for them, over
// `org.gnome.SettingsDaemon.MediaKeys` — an interface that predates MPRIS and
// has outlived every attempt to retire it.
//
// Measured rather than assumed: with the MPRIS service of F-050 running and
// correct, a synthesised `XF86AudioPlay` left the player exactly where it was.
// The daemon had the key and nothing had asked it for one.
//
// So the order of preference is the daemon first and MPRIS second, and the two
// cover different desktops rather than different display servers:
//
//   * GNOME, Cinnamon, MATE — the daemon, on X11 and on Wayland alike.
//   * KDE and others that drive MPRIS directly — F-050, with nothing more.
//   * A bare window manager with neither — nothing here helps; that case wants
//     an X11 grab and is recorded in IMPROVEMENTS.md rather than written blind,
//     because a global grab that cannot be tested is a global grab that eats
//     somebody's keyboard.
//
// **The keys are claimed on activity, not on existence.** They are a single
// global thing that only one application can hold, and the daemon gives them to
// whoever registered last — so a player that registers at startup and holds on
// until it exits takes them from a browser that is actually playing something
// and never gives them back. That was IMP-009, and it was found by somebody
// pressing play for a video and hearing a silent music player answer.
//
// Ferrolux therefore holds them only while it is the session the user means:
// while its window has focus, and while it is a session that has actually
// played and still has something to resume. Idle and unfocused, it wants
// nothing and says so. Every other well-behaved player follows the same
// convention, which is what makes this self-balancing: whichever application
// most recently became the active media session ends up holding the keys, and
// neither has to know the other is there.
//
// "A session that has played" is doing real work in that sentence, and reading
// the engine's state is not enough to express it. A track loaded from the
// command line sits at `Paused` on row 0 having never made a sound, which is
// the same state as a session paused halfway through — so a rule written in
// terms of "paused with a track" reintroduces the exact behaviour it was meant
// to prevent, and does it at launch. What is remembered instead is whether
// playback has happened since the last stop.
//
// Registration is not permanent in the other direction either. The grab belongs
// to the D-Bus connection and lapses when the daemon restarts, so the service is
// watched and re-registered rather than claimed once and assumed.

#pragma once

#include <QObject>
#include <QString>

namespace ferrolux::app { class Player; }

namespace ferrolux::platform {

class MediaKeys : public QObject
{
    Q_OBJECT

public:
    explicit MediaKeys(app::Player *player, QObject *parent = nullptr);

    // Hands the keys back if they are held. The daemon would notice the
    // connection closing anyway, but only once the process is gone — saying so
    // first means whichever player registered before this one gets them back at
    // the moment this one stops wanting them.
    ~MediaKeys() override;

    // Finds a daemon and starts watching the things that decide whether the
    // keys are wanted. It does **not** register: that happens the first time
    // Ferrolux is actually the session in use. Returns whether a daemon is
    // there at all, which is not an error when it is not — on KDE the keys
    // arrive through MPRIS instead.
    //
    // `window` is optional and is read by property name, so `platform/` needs
    // no Qt Quick dependency to know whether the panel has focus. The same
    // arrangement `Settings` and `MprisService` use.
    bool attach(QObject *window = nullptr);

    // Which daemon was found, for the log. Empty when none was.
    QString provider() const { return m_available.service; }

    // Whether the keys are held right now. Exists for the tests and the log
    // rather than for callers: nothing should be asking permission.
    bool holding() const { return m_holding; }

private slots:
    // The daemon broadcasts to everyone and names the application it means, so
    // the name is checked. Without that, two registered players both act on one
    // press and the track advances twice.
    void keyPressed(const QString &application, const QString &key);

    // Re-decides whether the keys are wanted, and grabs or releases to match.
    // Connected to everything that can change the answer, and safe to call when
    // nothing has.
    void evaluate();

private:
    struct Daemon {
        QString service;
        QString path;
        QString interface;
    };

    // Is Ferrolux the media session the user means?
    bool wanted() const;

    bool grab();
    void release();
    void watch(const Daemon &daemon);

    app::Player *m_player = nullptr;
    QObject *m_window = nullptr;
    Daemon m_available;
    bool m_holding = false;

    // Whether playback has actually happened since the last stop, as distinct
    // from a track merely being loaded. `Engine::Paused` cannot tell those
    // apart and both are the state a freshly launched player is in.
    bool m_played = false;
};

} // namespace ferrolux::platform
