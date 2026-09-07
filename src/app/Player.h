// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// app/Player.h — the wiring, given a name.
//
// `core/`, `library/`, `meters/` and `ui/` do not include one another. That is
// not an accident of how they grew: each is testable on its own precisely
// because it has no opinion about the others, and until now `main()` was the
// only code in the project that knew all four existed. The arrows between them
// were a screenful of lambdas in one function, which was a virtue while there
// was one consumer of them.
//
// Phase 6 adds four more — MPRIS2 (F-050), media keys (F-051), single-instance
// enqueue (F-052) and session restore (F-015) — and every one of them needs to
// both observe and command the same objects. Wiring four desktop services into
// `main()` alongside the graph they act on is how that function stops being
// readable, which is what IMP-005 predicted.
//
// So `app/` is the one module allowed to know about more than one of the peers,
// and `Player` is the whole of it. It owns the objects, holds the arrows between
// them, and offers the commands a desktop service needs. `platform/` talks to
// this and never to `core/` directly, which is what keeps the D-Bus and session
// code out of everything that makes sound.
//
// **It adds no policy.** Every rule about what plays next still belongs to
// `PlaylistModel`, and every rule about how it is played still belongs to
// `Engine`. A method here that decided something either of them should decide
// would be a third place to look for the answer, which is the failure this
// class exists to prevent rather than a shortcut it may take.

#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

#include "core/Engine.h"
#include "library/MetadataReader.h"
#include "library/PlaylistFilter.h"
#include "library/PlaylistModel.h"
#include "meters/MeterSource.h"

namespace ferrolux::app {

class Player : public QObject
{
    Q_OBJECT

public:
    // What should happen to a list of paths arriving from outside — the command
    // line today, a second instance once F-052 lands.
    //
    // Named for what each one *does* rather than for the flag that will select
    // it. Which flag maps to which of these is F-052's decision and is not
    // settled; encoding a guess about it in these names would put that decision
    // here, where nobody would think to look for it. The one thing already
    // settled is that the default does not play — see BUG-015, where an implied
    // default drifted to auto-play and had to be found by use.
    enum Open {
        AddAndSelect,   // append, select the first arrival, play nothing
        AddOnly,        // append; whatever is playing carries on
        AddAndPlay,     // append and start at the first arrival
        ReplaceAndPlay, // empty the list first, then AddAndPlay
    };
    Q_ENUM(Open)

    explicit Player(QObject *parent = nullptr);

    // The objects themselves, for the QML context and for `platform/`. Handed
    // out rather than proxied: a facade that re-exported forty properties would
    // be a second copy of four public interfaces to keep in step, and the point
    // of this class is the arrows, not the nouns.
    core::Engine *engine() { return &m_engine; }
    core::Equaliser *equaliser() { return m_engine.equaliser(); }
    library::PlaylistModel *playlist() { return &m_playlist; }
    library::PlaylistFilter *view() { return &m_view; }
    meters::MeterSource *meters() { return &m_meters; }

public slots:
    // The transport surface MPRIS2 and the media keys both need. Thin on
    // purpose: each one is the call `main()` already made, or the call the panel
    // already makes, so a desktop service and a button on the panel go through
    // the same path and cannot diverge.
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(qint64 positionNs);

    // One key and one D-Bus method both mean "the other thing from now". The
    // engine's state is the authority, so this stays right however playback came
    // to be where it is.
    void playPause();

    void open(const QList<QUrl> &paths, Open how = AddAndSelect);

private:
    // Every connection between the peers, in one place and made once.
    void wire();

    // How many entries have failed since anything last played. A file that will
    // not play is stepped over (F-001, BUG-025), and a playlist where none of
    // them will play would otherwise be walked end to end at the speed
    // GStreamer can refuse them. One full pass is the bound: after that there
    // is nothing to advance *to*, and continuing would be a loop rather than a
    // search.
    int m_failuresSinceProgress = 0;

    // Declaration order is destruction order reversed, and that matters here.
    // `MetadataReader` runs a worker pool that signals into `m_playlist`, and
    // `m_view` proxies it; both must go before the model they refer to. The
    // engine goes last of all because it owns every GStreamer object in the
    // process, and `gst_deinit()` deadlocks if a pipeline outlives it — BUG-012.
    core::Engine m_engine;
    library::PlaylistModel m_playlist;
    meters::MeterSource m_meters;
    library::PlaylistFilter m_view;
    library::MetadataReader m_metadata;
};

} // namespace ferrolux::app
