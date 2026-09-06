// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/MprisService.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

#include "app/Player.h"
#include "core/Engine.h"
#include "library/PlaylistModel.h"

namespace ferrolux::platform {

using core::Engine;
using library::PlaylistModel;

namespace {

const auto kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const auto kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const auto kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// The specification's own placeholder for "nothing is loaded". A client that
// receives an empty metadata map with no `mpris:trackid` is entitled to ignore
// the whole map, so the identifier is always present even when there is no
// track to identify.
const auto kNoTrack = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");

constexpr qint64 kNsPerUs = 1000;

} // namespace

// ---- root adaptor ----------------------------------------------------------

MprisRoot::MprisRoot(MprisService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
}

bool MprisRoot::canRaise() const
{
    // Answered from the fact rather than asserted. Without a window there is
    // nothing to raise, and a client that is told otherwise will offer the
    // action and watch it do nothing.
    return m_service->window() != nullptr;
}

QString MprisRoot::identity() const
{
    return QStringLiteral("Ferrolux RS-1");
}

QStringList MprisRoot::supportedUriSchemes() const
{
    return { QStringLiteral("file") };
}

QStringList MprisRoot::supportedMimeTypes() const
{
    // F-001's format list, as media types. Coverage is GStreamer's per D-002,
    // so this is what the application offers to open rather than a promise
    // that every one of them will decode on a given machine.
    return { QStringLiteral("audio/flac"),      QStringLiteral("audio/mpeg"),
             QStringLiteral("audio/ogg"),       QStringLiteral("audio/x-vorbis+ogg"),
             QStringLiteral("audio/x-opus+ogg"), QStringLiteral("audio/mp4"),
             QStringLiteral("audio/aac"),       QStringLiteral("audio/x-wav"),
             QStringLiteral("audio/x-aiff"),    QStringLiteral("audio/x-wavpack"),
             QStringLiteral("audio/x-musepack") };
}

void MprisRoot::Raise()
{
    QObject *window = m_service->window();
    if (!window)
        return;
    window->setProperty("visible", true);
    QMetaObject::invokeMethod(window, "raise");
    QMetaObject::invokeMethod(window, "requestActivate");
}

void MprisRoot::Quit()
{
    QCoreApplication::quit();
}

// ---- player adaptor --------------------------------------------------------

MprisPlayer::MprisPlayer(MprisService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
}

QString MprisPlayer::playbackStatus() const
{
    switch (m_service->player()->engine()->state()) {
    case Engine::Playing:
        return QStringLiteral("Playing");
    case Engine::Paused:
        return QStringLiteral("Paused");
    case Engine::Loading:
        // Loading is on its way to playing, and MPRIS has no third answer. A
        // client told "Stopped" here would draw a play button over a track that
        // is about to start by itself.
        return QStringLiteral("Playing");
    case Engine::Stopped:
    case Engine::Error:
        break;
    }
    return QStringLiteral("Stopped");
}

QString MprisPlayer::loopStatus() const
{
    switch (m_service->player()->playlist()->repeat()) {
    case PlaylistModel::RepeatOne: return QStringLiteral("Track");
    case PlaylistModel::RepeatAll: return QStringLiteral("Playlist");
    case PlaylistModel::RepeatOff: break;
    }
    return QStringLiteral("None");
}

void MprisPlayer::setLoopStatus(const QString &status)
{
    PlaylistModel *playlist = m_service->player()->playlist();
    if (status == QLatin1String("Track"))
        playlist->setRepeat(PlaylistModel::RepeatOne);
    else if (status == QLatin1String("Playlist"))
        playlist->setRepeat(PlaylistModel::RepeatAll);
    else
        playlist->setRepeat(PlaylistModel::RepeatOff);
}

bool MprisPlayer::shuffle() const
{
    return m_service->player()->playlist()->shuffle();
}

void MprisPlayer::setShuffle(bool on)
{
    m_service->player()->playlist()->setShuffle(on);
}

QVariantMap MprisPlayer::metadata() const
{
    const PlaylistModel *playlist = m_service->player()->playlist();
    const Engine *engine = m_service->player()->engine();
    const int row = playlist->currentRow();

    QVariantMap map;
    if (row < 0) {
        map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(kNoTrack)));
        return map;
    }

    // A path unique to the entry, which is what the specification asks for and
    // what `SetPosition` is checked against. The row is enough because it is
    // what identifies an entry to the rest of the application; it changes when
    // the list is reordered, which is exactly when a client should consider its
    // cached metadata stale.
    map.insert(QStringLiteral("mpris:trackid"),
               QVariant::fromValue(QDBusObjectPath(
                   QStringLiteral("/org/mpris/MediaPlayer2/ferrolux/track/%1").arg(row))));

    // Microseconds, and from the engine rather than from a tag: it has demuxed
    // the stream and a tag can only estimate. SPEC.md §Duration.
    if (engine->duration() > 0)
        map.insert(QStringLiteral("mpris:length"), qlonglong(engine->duration() / kNsPerUs));

    const QString title = playlist->currentTitle();
    if (!title.isEmpty())
        map.insert(QStringLiteral("xesam:title"), title);

    // An array even for one artist, because the specification types it as one
    // and a client that reads `as` will discard a plain string without a word.
    const QString artist = playlist->currentArtist();
    if (!artist.isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{ artist });

    const QString album = playlist->currentAlbum();
    if (!album.isEmpty())
        map.insert(QStringLiteral("xesam:album"), album);

    const QUrl source = engine->source();
    if (!source.isEmpty())
        map.insert(QStringLiteral("xesam:url"), source.toString());

    return map;
}

double MprisPlayer::volume() const { return m_service->player()->engine()->volume(); }

void MprisPlayer::setVolume(double level)
{
    m_service->player()->engine()->setVolume(qBound(0.0, level, 1.0));
}

qlonglong MprisPlayer::position() const
{
    return qlonglong(m_service->player()->engine()->position() / kNsPerUs);
}

bool MprisPlayer::canGoNext() const { return m_service->player()->playlist()->rowCount() > 0; }
bool MprisPlayer::canGoPrevious() const { return m_service->player()->playlist()->rowCount() > 0; }
bool MprisPlayer::canPlay() const { return m_service->player()->playlist()->rowCount() > 0; }

bool MprisPlayer::canPause() const
{
    return m_service->player()->engine()->state() != Engine::Stopped;
}

bool MprisPlayer::canSeek() const { return m_service->player()->engine()->isSeekable(); }

void MprisPlayer::Next() { m_service->player()->next(); }
void MprisPlayer::Previous() { m_service->player()->previous(); }
void MprisPlayer::Pause() { m_service->player()->pause(); }
void MprisPlayer::PlayPause() { m_service->player()->playPause(); }
void MprisPlayer::Stop() { m_service->player()->stop(); }
void MprisPlayer::Play() { m_service->player()->play(); }

void MprisPlayer::Seek(qlonglong offsetUs)
{
    // Relative, and may be negative. The engine clamps into range, so seeking
    // back past the start lands at the start rather than being refused.
    Engine *engine = m_service->player()->engine();
    engine->seek(engine->position() + offsetUs * kNsPerUs);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    // The identifier is checked rather than ignored. A client that computed a
    // position against the previous track — which is easy, because a track can
    // change between reading the metadata and sending the call — would
    // otherwise scrub the new one to an arbitrary point.
    const QVariant current = metadata().value(QStringLiteral("mpris:trackid"));
    if (current.value<QDBusObjectPath>().path() != trackId.path())
        return;

    m_service->player()->engine()->seek(positionUs * kNsPerUs);
}

void MprisPlayer::OpenUri(const QString &uri)
{
    const QUrl url(uri);
    if (!url.isValid())
        return;

    // Asked for by name, so it plays. This is the one entry point where that is
    // right: a client calling `OpenUri` has been told to open something, which
    // is not the bare command line of BUG-015.
    m_service->player()->open({ url }, app::Player::AddAndPlay);
}

// ---- service ---------------------------------------------------------------

MprisService::MprisService(app::Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    new MprisRoot(this);
    m_playerAdaptor = new MprisPlayer(this);
}

bool MprisService::attach(QObject *window)
{
    m_window = window;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_error = QStringLiteral("no session bus; desktop media controls will not be available");
        return false;
    }

    if (!bus.registerObject(kObjectPath, this)) {
        m_error = QStringLiteral("could not register %1 on the session bus").arg(kObjectPath);
        return false;
    }

    // The specification allows a second instance to qualify its name, and
    // taking the qualified one is better than failing: two players running at
    // once is a normal thing to do, and the second losing its controls entirely
    // is not.
    const auto plain = QStringLiteral("org.mpris.MediaPlayer2.ferrolux");
    if (!bus.registerService(plain)) {
        const auto qualified = QStringLiteral("%1.instance%2")
                                   .arg(plain).arg(QCoreApplication::applicationPid());
        if (!bus.registerService(qualified)) {
            m_error = QStringLiteral("could not claim %1 or %2").arg(plain, qualified);
            bus.unregisterObject(kObjectPath);
            return false;
        }
    }

    m_attached = true;
    observe();
    return true;
}

void MprisService::observe()
{
    Engine *engine = m_player->engine();
    PlaylistModel *playlist = m_player->playlist();

    // Qt emits nothing for an adaptor's properties, so every one of these is a
    // signal the application already had, turned into the one the bus expects.
    // A client that subscribes rather than polls sees only what is listed here.

    connect(engine, &Engine::stateChanged, this, [this] {
        notify({ { QStringLiteral("PlaybackStatus"), m_playerAdaptor->playbackStatus() },
                 { QStringLiteral("CanPause"), m_playerAdaptor->canPause() } });
    });

    // The tags, the source and the duration are three signals and one fact.
    // They are coalesced rather than forwarded one for one.
    connect(playlist, &PlaylistModel::nowPlayingChanged, this, &MprisService::scheduleMetadata);
    connect(engine, &Engine::durationChanged, this, &MprisService::scheduleMetadata);
    connect(engine, &Engine::sourceChanged, this, &MprisService::scheduleMetadata);

    connect(engine, &Engine::volumeChanged, this, [this] {
        notify({ { QStringLiteral("Volume"), m_playerAdaptor->volume() } });
    });
    connect(engine, &Engine::seekableChanged, this, [this] {
        notify({ { QStringLiteral("CanSeek"), m_playerAdaptor->canSeek() } });
    });
    connect(playlist, &PlaylistModel::shuffleChanged, this, [this] {
        notify({ { QStringLiteral("Shuffle"), m_playerAdaptor->shuffle() } });
    });
    connect(playlist, &PlaylistModel::repeatChanged, this, [this] {
        notify({ { QStringLiteral("LoopStatus"), m_playerAdaptor->loopStatus() } });
    });
    connect(playlist, &PlaylistModel::countChanged, this, [this] {
        notify({ { QStringLiteral("CanGoNext"), m_playerAdaptor->canGoNext() },
                 { QStringLiteral("CanGoPrevious"), m_playerAdaptor->canGoPrevious() },
                 { QStringLiteral("CanPlay"), m_playerAdaptor->canPlay() } });
    });

    // `Position` is deliberately not among them. The specification says so —
    // it changes continuously and a client is expected to extrapolate — and a
    // property signal at the frame rate would be a bus message every 16 ms.
    // What a client cannot extrapolate is a jump, which is what `Seeked` is
    // for, and why `Engine` grew a signal that distinguishes one from ordinary
    // progress.
    connect(engine, &Engine::seeked, this, [this](qint64 positionNs) {
        emit m_playerAdaptor->Seeked(qlonglong(positionNs / kNsPerUs));
    });
}

void MprisService::scheduleMetadata()
{
    if (m_metadataPending)
        return;
    m_metadataPending = true;

    // Deferred to the end of this turn of the event loop, which is correctness
    // before it is economy. A track change arrives as three separate signals,
    // and `metadata()` reads all of the underlying state each time — so sending
    // one straight from the first of them publishes a map that is internally
    // inconsistent: the new title against the previous track's URL, because the
    // tags had changed and the source had not yet. It settles a moment later,
    // which is exactly what makes it the kind of defect nobody reports.
    //
    // Waiting until every one of them has landed also means one signal instead
    // of three, and a lock screen rebuilds its layout once per track.
    QTimer::singleShot(0, this, [this] {
        m_metadataPending = false;
        notify({ { QStringLiteral("Metadata"), m_playerAdaptor->metadata() } });
    });
}

void MprisService::notify(const QVariantMap &changed)
{
    if (!m_attached)
        return;

    QDBusMessage signal = QDBusMessage::createSignal(kObjectPath, kPropertiesInterface,
                                                     QStringLiteral("PropertiesChanged"));
    signal << kPlayerInterface << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

} // namespace ferrolux::platform
