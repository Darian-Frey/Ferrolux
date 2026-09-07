// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/Session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

#include "app/Player.h"
#include "core/Engine.h"
#include "library/PlaylistModel.h"

namespace ferrolux::platform {

using core::Engine;
using library::PlaylistModel;

namespace {

Q_LOGGING_CATEGORY(lcSession, "ferrolux.platform.session")

} // namespace

Session::Session(app::Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
}

QString Session::playlistPath()
{
    // Beside the data rather than beside the settings: this is a playlist that
    // happens to be written automatically, not a preference.
    //
    // Built from the generic location and the application name rather than from
    // `AppDataLocation`, which appends the organisation *and* the application
    // and so yields `.../ferrolux/ferrolux/` when the two are the same string.
    // That is not wrong, but it does not match `~/.config/ferrolux/` where the
    // settings live, and a user looking for their session should find it beside
    // everything else the application owns.
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1Char('/') + QCoreApplication::applicationName();
    return directory + QStringLiteral("/session.m3u8");
}

bool Session::restore()
{
    const QSettings settings;

    const QString path = settings.value(QStringLiteral("session/playlist")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;

    PlaylistModel *playlist = m_player->playlist();
    if (!playlist->loadFrom(QUrl::fromLocalFile(path)))
        return false;
    if (playlist->rowCount() == 0)
        return false;

    // Read before the order is adopted, because adopting it selects a row and
    // that is the last thing to happen.
    m_position = settings.value(QStringLiteral("session/position"), 0).toLongLong();
    const int track = settings.value(QStringLiteral("session/track"), -1).toInt();

    QList<int> order;
    const QVariantList stored = settings.value(QStringLiteral("session/order")).toList();
    order.reserve(stored.size());
    for (const QVariant &value : stored)
        order.append(value.toInt());

    // Absent means sequential, which is the identity permutation and is what
    // `loadFrom` has already built. Writing twenty thousand consecutive
    // integers into a settings file to say "in order" would be a cost paid by
    // every user who does not use shuffle.
    if (order.isEmpty()) {
        order.resize(playlist->rowCount());
        for (int i = 0; i < order.size(); ++i)
            order[i] = i;
    }

    if (!playlist->adoptOrder(order, track)) {
        // The saved order does not describe the playlist that came back with
        // it. Keep the contents — they are the expensive part and they are
        // fine — and let the model's own order stand.
        qCWarning(lcSession) << "saved play order does not match the restored playlist;"
                             << "keeping the contents and rebuilding the order";
        if (track >= 0 && track < playlist->rowCount())
            playlist->selectWithoutPlaying(track);
        m_position = 0;
    }

    qCInfo(lcSession) << "restored" << playlist->rowCount() << "entries at row" << track;
    return true;
}

void Session::resume()
{
    if (m_position <= 0)
        return;

    Engine *engine = m_player->engine();
    const qint64 position = m_position;

    // A seek cannot happen until the pipeline has the stream open and says it
    // is seekable, which is several state changes after `restore` returns.
    // Waiting for the signal is the only honest way to know: a timer would be a
    // guess, and a guess that is too short leaves the position at zero with
    // nothing to show that anything was attempted.
    auto *once = new QMetaObject::Connection;
    *once = connect(engine, &Engine::seekableChanged, this, [engine, position, once] {
        if (!engine->isSeekable())
            return;
        engine->seek(position);
        disconnect(*once);
        delete once;
    });

    // Already seekable, which happens when the stream opened while the caller
    // was still deciding whether to resume.
    if (engine->isSeekable()) {
        engine->seek(position);
        disconnect(*once);
        delete once;
    }
}

void Session::save()
{
    QSettings settings;
    const PlaylistModel *playlist = m_player->playlist();

    if (playlist->rowCount() == 0) {
        // Nothing to come back to. The keys are removed rather than left
        // pointing at a stale file, so that the next launch is a clean start
        // instead of restoring a playlist the user emptied on purpose.
        settings.remove(QStringLiteral("session/playlist"));
        settings.remove(QStringLiteral("session/track"));
        settings.remove(QStringLiteral("session/order"));
        settings.remove(QStringLiteral("session/position"));
        QFile::remove(playlistPath());
        return;
    }

    const QString path = playlistPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!m_player->playlist()->saveTo(QUrl::fromLocalFile(path))) {
        qCWarning(lcSession) << "could not write the session playlist to" << path;
        return;
    }

    settings.setValue(QStringLiteral("session/playlist"), path);
    settings.setValue(QStringLiteral("session/track"), playlist->currentRow());
    settings.setValue(QStringLiteral("session/position"), m_player->engine()->position());

    // Only when it is not the identity. See `restore` — sequential order is
    // rebuilt for nothing, and this is the one key that grows with the playlist.
    const QList<int> order = playlist->playOrder();
    bool sequential = true;
    for (int i = 0; i < order.size() && sequential; ++i)
        sequential = order.at(i) == i;

    if (sequential) {
        settings.remove(QStringLiteral("session/order"));
    } else {
        QVariantList stored;
        stored.reserve(order.size());
        for (int value : order)
            stored.append(value);
        settings.setValue(QStringLiteral("session/order"), stored);
    }
}

} // namespace ferrolux::platform
