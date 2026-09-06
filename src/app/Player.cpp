// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "app/Player.h"

namespace ferrolux::app {

using core::Engine;
using library::PlaylistModel;

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_view.setSourceModel(&m_playlist);
    wire();
}

void Player::wire()
{
    // ---- meters ------------------------------------------------------------
    // The analysis elements run ahead of the sink, so their figures are queued
    // against the running time they carry and released when the clock reaches
    // them. Applying on arrival put the display over a second ahead of the
    // audio, which is BUG-011.
    connect(&m_engine, &Engine::levelMeasured, &m_meters,
            [this](const QList<double> &rms, const QList<double> &peak,
                   const QList<double> &decay, qint64 runningTime) {
                m_meters.queueLevel(rms, peak, decay, runningTime);
            });
    connect(&m_engine, &Engine::spectrumMeasured, &m_meters,
            [this](const QList<float> &magnitudes, int rate, qint64 runningTime) {
                m_meters.queueSpectrum(magnitudes, rate, runningTime);
            });

    // Stopping releases the display to rest; pausing holds it. The engine's own
    // state is the authority, so this stays right however playback came to a
    // halt — the end of a playlist, a file that failed, or the button.
    connect(&m_engine, &Engine::stateChanged, &m_meters, [this] {
        const auto state = m_engine.state();
        m_meters.setReleasing(state == Engine::Stopped || state == Engine::Error);
    });

    // ---- metadata ----------------------------------------------------------
    // The model asks, the reader answers on a worker pool, the model applies the
    // results. Neither knows anything about the other's threading.
    connect(&m_playlist, &PlaylistModel::metadataNeeded,
            &m_metadata, &library::MetadataReader::enqueue);
    connect(&m_metadata, &library::MetadataReader::batchReady,
            &m_playlist, &PlaylistModel::applyMetadata);

    // ---- playback ----------------------------------------------------------
    // The playlist decides what plays, the engine is told. Invariant 5.
    connect(&m_playlist, &PlaylistModel::currentEntryChanged, &m_engine,
            [this](const QUrl &url) {
                if (url.isEmpty())
                    return;
                m_engine.setSource(url);
                m_engine.play();
            });

    // Gapless: the next URI is cached ahead of time so that the streaming thread
    // never has to ask the model for it. See F-005 and AV-006. Prepared rather
    // than started — the entry is loaded and the position bar and duration
    // populate, but nothing is heard until Play.
    connect(&m_playlist, &PlaylistModel::currentEntryPrepared, &m_engine,
            [this](const QUrl &url) {
                if (!url.isEmpty())
                    m_engine.setSource(url);
            });

    connect(&m_playlist, &PlaylistModel::nextEntryChanged,
            &m_engine, &Engine::setNextSource);
    connect(&m_engine, &Engine::gaplessAdvance, &m_playlist,
            [this] { m_playlist.advanceForHandover(); });

    // A track that ends without a handover — the last of a list, or a file that
    // failed — advances normally, which does start playback.
    connect(&m_engine, &Engine::endOfStream, &m_playlist,
            [this] { m_playlist.advance(); });
    connect(&m_engine, &Engine::previousTrackRequested, &m_playlist,
            [this] { m_playlist.retreat(); });

    // The engine demuxes the stream and so knows the real duration; a tag can
    // only estimate it. SPEC.md §Duration makes this the authoritative source.
    connect(&m_engine, &Engine::durationChanged, &m_playlist, [this] {
        m_playlist.setAuthoritativeDuration(m_engine.source(), m_engine.duration());
    });
}

// ---- transport -------------------------------------------------------------
// Each of these is the call the panel already makes. A desktop service and a
// button on the chassis therefore go down the same path, and there is no second
// implementation of "next" to drift out of step with the first.

void Player::play() { m_engine.play(); }
void Player::pause() { m_engine.pause(); }
void Player::stop() { m_engine.stop(); }
void Player::seek(qint64 positionNs) { m_engine.seek(positionNs); }

// Next belongs to the playlist because play order does — invariant 5. Previous
// belongs to the engine because F-002 puts a rule on it that only the engine can
// apply: within the first three seconds it means the previous track, and after
// them it means the start of this one. The engine answers by emitting
// `previousTrackRequested`, which `wire()` routes back to the model.
void Player::next() { m_playlist.advance(); }
void Player::previous() { m_engine.previous(); }

void Player::playPause()
{
    if (m_engine.state() == Engine::Playing)
        m_engine.pause();
    else
        m_engine.play();
}

void Player::open(const QList<QUrl> &paths, Open how)
{
    if (paths.isEmpty())
        return;

    if (how == ReplaceAndPlay)
        m_playlist.clear();

    // Where the arrivals will land. `addPaths` expands directories and appends,
    // synchronously, so the row count taken now is the first of them — which is
    // the row to select, not row zero. Selecting zero would be right only for an
    // empty list, and silently wrong for every enqueue into a full one.
    const int first = m_playlist.rowCount();
    m_playlist.addPaths(paths);
    if (m_playlist.rowCount() <= first)
        return; // nothing in them was playable

    switch (how) {
    case AddOnly:
        break;
    case AddAndSelect:
        // Loaded and shown, but silent. Handing over a directory of several
        // hundred files and having audio begin unbidden is a surprise, and
        // F-052's explicit play form only means something if the default is not
        // that. See BUG-015.
        m_playlist.selectWithoutPlaying(first);
        break;
    case AddAndPlay:
    case ReplaceAndPlay:
        // `setCurrentRow` emits `currentEntryChanged`, which `wire()` turns into
        // a source and a play. Nothing here calls `play()` itself: one route to
        // starting playback, whoever asked for it.
        m_playlist.setCurrentRow(first);
        break;
    }
}

} // namespace ferrolux::app
