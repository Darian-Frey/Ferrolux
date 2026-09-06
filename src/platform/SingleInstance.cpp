// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/SingleInstance.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QUrl>

#include "app/Player.h"
#include "platform/CommandLine.h"

namespace ferrolux::platform {

namespace {

Q_LOGGING_CATEGORY(lcInstance, "ferrolux.platform.instance")

const auto kService = QStringLiteral("org.ferrolux.Ferrolux");
const auto kPath = QStringLiteral("/org/ferrolux/Ferrolux");
const auto kInterface = QStringLiteral("org.ferrolux.Ferrolux");

} // namespace

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstance::claim()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_available = bus.isConnected();
    if (!m_available) {
        // Nothing to coordinate through. Carrying on is the only option that
        // plays the file the user asked for.
        qCInfo(lcInstance) << "no session bus; this launch is its own player";
        return true;
    }

    m_owner = bus.registerService(kService);
    return m_owner;
}

bool SingleInstance::handOff(const QStringList &paths, const QString &mode)
{
    if (!m_available || m_owner)
        return false;

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface,
                                                       QStringLiteral("Open"));
    call << paths << mode;

    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        // The name was taken but nobody answered — a player still starting up,
        // or one that died without releasing it. Starting normally loses
        // nothing; exiting here would lose the files.
        qCWarning(lcInstance) << "the running player did not answer:" << reply.errorMessage();
        return false;
    }

    return true;
}

void SingleInstance::serve(app::Player *player, QObject *window)
{
    m_player = player;
    m_window = window;

    if (!m_owner)
        return;

    if (!QDBusConnection::sessionBus().registerObject(kPath, this,
                                                      QDBusConnection::ExportAllSlots)) {
        qCWarning(lcInstance) << "could not publish" << kPath
                              << "— further launches will start their own player";
    }
}

void SingleInstance::Open(const QStringList &paths, const QString &mode)
{
    if (!m_player)
        return;

    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths)
        urls.append(QUrl(path));

    // The bare default arrives as `select`, and selecting the first arrival is
    // right only when there was nothing to disturb. Into a player that already
    // has a playlist it becomes an enqueue: the user opened a file, they did
    // not ask for the cursor to leave whatever is playing.
    app::Player::Open how = CommandLine::mode(mode);
    if (how == app::Player::AddAndSelect && m_player->playlist()->rowCount() > 0)
        how = app::Player::AddOnly;

    m_player->open(urls, how);

    // Brought forward, because a file opened from a file manager that
    // disappears into a window behind three others has not visibly worked.
    if (m_window) {
        m_window->setProperty("visible", true);
        QMetaObject::invokeMethod(m_window, "raise");
        QMetaObject::invokeMethod(m_window, "requestActivate");
    }
}

} // namespace ferrolux::platform
