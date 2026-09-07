// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/MprisService.h — the player, as the desktop sees it. F-050.
//
// MPRIS2 is what makes the media keys on a keyboard work, what puts a track
// title on a lock screen, and what lets a panel applet show transport buttons.
// The specification is freedesktop's rather than this project's, which is why
// there is no SPEC.md section for it: the interface names, the property names
// and the units are all fixed from outside, and a value invented here would
// simply not be read.
//
// Two things about it are easy to get wrong and are handled explicitly below.
// **Everything is microseconds**, while `Engine` works in nanoseconds — the
// conversion happens at this boundary and nowhere else. And **Qt does not emit
// `PropertiesChanged` for adaptor properties**: an adaptor is a passive view
// onto a `Q_PROPERTY`, so a client that subscribes rather than polls sees
// nothing at all unless the signal is constructed and sent by hand. A player
// that looks correct when a client polls and stale when it subscribes is the
// characteristic MPRIS defect, and it is invisible from the application's own
// interface.
//
// This talks to `app::Player` and to nothing else. It holds no playback logic:
// every method here is one call onto the facade, which is the same call the
// panel's own buttons make, so a lock screen and a button on the chassis cannot
// diverge.

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace ferrolux::app { class Player; }

namespace ferrolux::platform {

class MprisService;

// org.mpris.MediaPlayer2 — the application, rather than the playback.
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

    // The basename of the installed `.desktop` file, without the suffix. It is
    // how a shell finds the icon and the name to put beside its media controls,
    // and it was deliberately absent until that file existed — a property
    // naming a file nobody has is worse than no property, because the fallback
    // to `Identity` only happens when the property is missing rather than wrong.
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)

public:
    explicit MprisRoot(MprisService *service);

    bool canQuit() const { return true; }
    bool canRaise() const;
    bool hasTrackList() const { return false; } // F-053 territory, not RS-1's
    QString identity() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;
    QString desktopEntry() const;

public slots:
    void Raise();
    void Quit();

private:
    MprisService *m_service;
};

// org.mpris.MediaPlayer2.Player — the playback itself.
class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayer(MprisService *service);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);

    // Rate is fixed at 1.0. `playbin3` can vary it, but nothing in RS-1 offers
    // the control and a player that advertises a range it does not implement is
    // worse than one that advertises none.
    double rate() const { return 1.0; }
    void setRate(double) {}
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }

    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;

    // MPRIS volume and the panel's volume are both 0..1 and both are what the
    // user sees, so they map straight across. Neither is an amplitude: SPEC.md
    // §Volume taper puts a cubic between the position and the gain, and it
    // stays on the far side of `Engine`.
    double volume() const;
    void setVolume(double level);

    qlonglong position() const;

    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qlonglong positionUs);

private:
    MprisService *m_service;
};

class MprisService : public QObject
{
    Q_OBJECT

public:
    // `window` is optional and is only used for `Raise`. Taken as a plain
    // QObject and invoked by name, so `platform/` needs no Qt Quick dependency
    // to bring a window forward — the same arrangement `Settings` uses.
    explicit MprisService(app::Player *player, QObject *parent = nullptr);

    // Registers the object and claims the bus name. Returns false and says why
    // in `lastError` if there is no session bus or the name cannot be had.
    // **A failure here is not fatal**: the player works without a desktop, and
    // refusing to start because a headless session has no bus would be a worse
    // outcome than losing the lock-screen controls.
    bool attach(QObject *window = nullptr);
    QString lastError() const { return m_error; }

    app::Player *player() const { return m_player; }
    QObject *window() const { return m_window; }

    // Builds and sends `org.freedesktop.DBus.Properties.PropertiesChanged` for
    // the Player interface. Qt will not do this for an adaptor's properties.
    void notify(const QVariantMap &changed);

private:
    void observe();

    // One `Metadata` signal per turn of the event loop, however many of the
    // underlying facts changed. See the definition for why that is correctness
    // and not only economy.
    void scheduleMetadata();

    app::Player *m_player = nullptr;
    QObject *m_window = nullptr;
    MprisPlayer *m_playerAdaptor = nullptr;
    QString m_error;
    bool m_attached = false;
    bool m_metadataPending = false;
};

} // namespace ferrolux::platform
