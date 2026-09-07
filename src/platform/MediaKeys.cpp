// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/MediaKeys.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

#include "app/Player.h"
#include "core/Engine.h"
#include "library/PlaylistModel.h"
#include "platform/X11MediaKeys.h"

namespace ferrolux::platform {

using core::Engine;
using library::PlaylistModel;

namespace {

Q_LOGGING_CATEGORY(lcKeys, "ferrolux.platform.keys")

// The application name handed to the daemon and echoed back in every signal.
// It is an identity rather than a label: the daemon routes by it.
const auto kApplication = QStringLiteral("Ferrolux");

} // namespace

MediaKeys::MediaKeys(app::Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
}

MediaKeys::~MediaKeys()
{
    release();
}

bool MediaKeys::attach(QObject *window)
{
    m_window = window;

    // In order. GNOME's own name first, then the compatibility name its daemon
    // also claims — Cinnamon answers on that one, which is why the two are not
    // the same entry — then MATE's, which forked the interface wholesale and
    // changed only the strings.
    static const QList<Daemon> daemons = {
        { QStringLiteral("org.gnome.SettingsDaemon.MediaKeys"),
          QStringLiteral("/org/gnome/SettingsDaemon/MediaKeys"),
          QStringLiteral("org.gnome.SettingsDaemon.MediaKeys") },
        { QStringLiteral("org.gnome.SettingsDaemon"),
          QStringLiteral("/org/gnome/SettingsDaemon/MediaKeys"),
          QStringLiteral("org.gnome.SettingsDaemon.MediaKeys") },
        { QStringLiteral("org.mate.SettingsDaemon"),
          QStringLiteral("/org/mate/SettingsDaemon/MediaKeys"),
          QStringLiteral("org.mate.SettingsDaemon.MediaKeys") },
    };

    // Only if there is a bus to ask. A session with none is not an error and is
    // exactly where the fallback below earns its keep: **this used to return
    // here**, so the one case a global grab exists for — a bare window manager,
    // which frequently has no session bus either — was the one case that never
    // reached it.
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const Daemon &daemon : (bus.isConnected() ? daemons : QList<Daemon>{})) {
        // Watched whether or not it is running now. A daemon that restarts
        // silently drops every grab it was holding, and a player that
        // registered once and then stops responding to the keys — with no
        // error anywhere — is the failure this avoids.
        watch(daemon);

        // Asked about rather than registered with. Nothing is claimed until
        // Ferrolux is the session in use; this only settles which daemon to
        // claim from when that happens.
        if (m_available.service.isEmpty()
            && bus.interface()->isServiceRegistered(daemon.service).value()) {
            m_available = daemon;
        }
    }

    // The three things that can change the answer to "are we the session the
    // user means". The playlist is in there because a list that empties leaves
    // a paused player with nothing to resume.
    connect(m_player->engine(), &Engine::stateChanged, this, &MediaKeys::evaluate);
    connect(m_player->playlist(), &PlaylistModel::currentRowChanged,
            this, &MediaKeys::evaluate);
    if (m_window)
        connect(m_window, SIGNAL(activeChanged()), this, SLOT(evaluate()));

    // No daemon on this session. That is a bare window manager, or a desktop
    // that expects MPRIS to carry the keys — F-050 covers the second, and the
    // first has nobody to ask, so the keys have to be taken. IMP-008.
    if (m_available.service.isEmpty() && X11MediaKeys::possible()) {
        m_grab = new X11MediaKeys(m_player, this);

        // **Taken once and held, unlike the daemon registration**, and the
        // difference is not an oversight. Handing a registration back gives the
        // keys to whichever player registered before this one; letting go of a
        // grab gives them to nobody, because there is no daemon on this session
        // to give them to. A polite grab is a key that does nothing at all.
        //
        // It also cannot be conditional on being the session in use, because
        // the condition can never become true: under a bare window manager
        // there may be no manager to focus the window, and nothing is playing
        // until a key starts it. Waiting to be wanted means waiting for
        // something that only the key being wanted could cause — which is
        // exactly what the first version of this did, and it did nothing at all
        // in the one session it exists for.
        if (!m_grab->grab())
            qCInfo(lcKeys) << "no media keys on this keyboard, or another client holds them";
    }

    // Once now, because a restored session or a `--play` on the command line
    // can already be playing by the time this runs.
    evaluate();

    return !m_available.service.isEmpty() || m_grab != nullptr;
}

bool MediaKeys::wanted() const
{
    // Looking at the panel counts. It is how the keys are taken back after a
    // browser has had them, and it is what makes a press of Play start an idle
    // player rather than doing nothing.
    if (m_window && m_window->property("active").toBool())
        return true;

    // Otherwise: a session that has actually played, and still has something to
    // resume. Paused counts, because pause is the state a player is left in
    // when the user means to come back to it — but only a pause that follows
    // playing. See `m_played`.
    return m_played && m_player->playlist()->currentRow() >= 0;
}

void MediaKeys::evaluate()
{
    // Only the daemon registration is claimed and released by activity. The
    // grab is held for the run — see `attach`.
    if (m_available.service.isEmpty())
        return;

    // **Paused means two different things, and only one of them has a claim.**
    // A track loaded from the command line sits Paused at row 0 having never
    // made a sound, which is indistinguishable by state alone from a session
    // the user paused halfway through. Reading the state was therefore enough
    // to reintroduce exactly the behaviour this was written to stop: a silent
    // player taking the keys from a browser at launch, which is IMP-009.
    //
    // So what is remembered is whether this has *been* a playing session.
    // Stopping ends it; loading and pausing leave it as it was.
    switch (m_player->engine()->state()) {
    case Engine::Playing:
        m_played = true;
        break;
    case Engine::Stopped:
    case Engine::Error:
        m_played = false;
        break;
    case Engine::Loading:
    case Engine::Paused:
        break;
    }

    const bool want = wanted();
    if (want && !m_holding)
        grab();
    else if (!want && m_holding)
        release();
}

bool MediaKeys::grab()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Connected before the grab, not after. The daemon may deliver a press
    // between the two, and a key that is grabbed but not listened for is a key
    // taken away from whatever had it before and given to nobody.
    const bool connected = bus.connect(m_available.service, m_available.path,
                                       m_available.interface,
                                       QStringLiteral("MediaPlayerKeyPressed"), this,
                                       SLOT(keyPressed(QString, QString)));
    if (!connected)
        return false;

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_available.service, m_available.path, m_available.interface,
        QStringLiteral("GrabMediaPlayerKeys"));

    // The timestamp orders competing registrations, and zero means "now" to
    // every implementation of this interface. Passing a real X server time
    // would tie platform/ to the display server for no gain.
    call << kApplication << 0u;

    const QDBusMessage reply = bus.call(call, QDBus::BlockWithGui, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        bus.disconnect(m_available.service, m_available.path, m_available.interface,
                       QStringLiteral("MediaPlayerKeyPressed"), this,
                       SLOT(keyPressed(QString, QString)));
        return false;
    }

    m_holding = true;
    qCDebug(lcKeys) << "took the media keys from" << m_available.service;
    return true;
}

void MediaKeys::release()
{
    if (!m_holding)
        return;

    QDBusConnection bus = QDBusConnection::sessionBus();

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_available.service, m_available.path, m_available.interface,
        QStringLiteral("ReleaseMediaPlayerKeys"));
    call << kApplication;

    // Sent rather than called: nothing is waiting on the answer, and blocking
    // on a daemon during shutdown is a way to hang on the way out.
    bus.send(call);
    bus.disconnect(m_available.service, m_available.path, m_available.interface,
                   QStringLiteral("MediaPlayerKeyPressed"), this,
                   SLOT(keyPressed(QString, QString)));

    m_holding = false;
    qCDebug(lcKeys) << "handed the media keys back";
}

void MediaKeys::watch(const Daemon &daemon)
{
    auto *watcher = new QDBusServiceWatcher(daemon.service, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this, daemon](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) {
                    // It went away, taking any grab with it. Forget both, so
                    // that whichever daemon comes back first can be used.
                    if (m_available.service == daemon.service) {
                        m_holding = false;
                        m_available = {};
                    }
                    return;
                }
                if (m_available.service.isEmpty()) {
                    m_available = daemon;
                    m_holding = false;
                    evaluate();
                }
            });
}

void MediaKeys::keyPressed(const QString &application, const QString &key)
{
    if (application != kApplication)
        return;

    // Every one of these is a call onto the facade, which is the same call the
    // panel's own buttons make. `Play` is the toggle rather than the command:
    // the daemon sends it for `XF86AudioPlay`, which on a keyboard with one
    // transport key is what that key means.
    if (key == QLatin1String("Play"))
        m_player->playPause();
    else if (key == QLatin1String("Pause"))
        m_player->pause();
    else if (key == QLatin1String("Stop"))
        m_player->stop();
    else if (key == QLatin1String("Next"))
        m_player->next();
    else if (key == QLatin1String("Previous"))
        m_player->previous();
    else
        qCDebug(lcKeys) << "ignoring media key" << key;
}

} // namespace ferrolux::platform
