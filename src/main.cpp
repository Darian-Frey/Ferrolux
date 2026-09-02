// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Ferrolux RS-1 — entry point.
//
// Wires the engine, the playlist and the metadata reader together and hands
// them to a throwaway QML harness. The connections below are the whole of the
// application's control flow, and they are deliberately all in one place:
// ARCHITECTURE.md gives the playlist ownership of play order and leaves the
// engine to be told what to play, so the arrows between them should be
// readable at a glance rather than scattered through both classes.
//
// Settings persistence lives here rather than in core/ so that core/ keeps no
// dependency on the desktop; platform/Settings takes it over in Phase 6. Keys
// and defaults are those in SPEC.md §Settings and are never invented locally.

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QUrl>

#include <algorithm>

#include <gst/gst.h>

#include "core/Engine.h"
#include "core/Equaliser.h"
#include "library/MetadataReader.h"
#include "library/PlaylistFilter.h"
#include "library/PlaylistModel.h"

using ferrolux::core::Engine;
using ferrolux::core::Equaliser;
using ferrolux::library::MetadataReader;
using ferrolux::library::PlaylistFilter;
using ferrolux::library::PlaylistModel;

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Engine engine;
    PlaylistModel playlist;
    MetadataReader metadata;
    PlaylistFilter view;
    view.setSourceModel(&playlist);

    // Metadata: the model asks, the reader answers on a worker pool, the model
    // applies the results. Neither knows anything about the other's threading.
    QObject::connect(&playlist, &PlaylistModel::metadataNeeded,
                     &metadata, &MetadataReader::enqueue);
    QObject::connect(&metadata, &MetadataReader::batchReady,
                     &playlist, &PlaylistModel::applyMetadata);

    // Playback: the playlist decides what plays, the engine is told.
    QObject::connect(&playlist, &PlaylistModel::currentEntryChanged, &engine,
                     [&engine](const QUrl &url) {
                         if (url.isEmpty())
                             return;
                         engine.setSource(url);
                         engine.play();
                     });

    // Gapless: the next URI is cached ahead of time so that the streaming
    // thread never has to ask the model for it. See F-005 and AV-006.
    QObject::connect(&playlist, &PlaylistModel::nextEntryChanged,
                     &engine, &Engine::setNextSource);
    QObject::connect(&engine, &Engine::gaplessAdvance, &playlist,
                     [&playlist] { playlist.advanceForHandover(); });

    // A track that ends without a handover — the last of a list, or a file that
    // failed — advances normally, which does start playback.
    QObject::connect(&engine, &Engine::endOfStream, &playlist,
                     [&playlist] { playlist.advance(); });
    QObject::connect(&engine, &Engine::previousTrackRequested, &playlist,
                     [&playlist] { playlist.retreat(); });

    // The engine demuxes the stream and so knows the real duration; a tag can
    // only estimate it. SPEC.md §Duration makes this the authoritative source.
    QObject::connect(&engine, &Engine::durationChanged, &playlist,
                     [&playlist, &engine] {
                         playlist.setAuthoritativeDuration(engine.source(), engine.duration());
                     });

    QSettings settings;
    engine.setVolume(settings.value(QStringLiteral("playback/volume"), 0.7).toDouble());
    engine.setBalance(settings.value(QStringLiteral("playback/balance"), 0.0).toDouble());
    playlist.setShuffle(settings.value(QStringLiteral("playback/shuffle"), false).toBool());
    const QString repeat = settings.value(QStringLiteral("playback/repeat"),
                                          QStringLiteral("off")).toString();
    playlist.setRepeat(repeat == QLatin1String("all")   ? PlaylistModel::RepeatAll
                       : repeat == QLatin1String("one") ? PlaylistModel::RepeatOne
                                                        : PlaylistModel::RepeatOff);

    Equaliser *equaliser = engine.equaliser();
    {
        // Band gains are restored before the enabled flag, so that switching on
        // applies the stored curve in one step rather than ramping to flat and
        // then to the real values.
        QList<double> storedBands;
        for (const QVariant &value : settings.value(QStringLiteral("equaliser/bands")).toList())
            storedBands.append(value.toDouble());
        if (storedBands.size() == Equaliser::kBandCount)
            equaliser->setBands(storedBands);

        equaliser->setPreamp(settings.value(QStringLiteral("equaliser/preamp"), 0.0).toDouble());
        equaliser->setEnabled(settings.value(QStringLiteral("equaliser/enabled"), false).toBool());
    }

    QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, [&engine, &playlist] {
        QSettings out;
        out.setValue(QStringLiteral("playback/volume"), engine.volume());
        out.setValue(QStringLiteral("playback/balance"), engine.balance());
        out.setValue(QStringLiteral("playback/shuffle"), playlist.shuffle());
        out.setValue(QStringLiteral("playback/repeat"),
                     playlist.repeat() == PlaylistModel::RepeatAll   ? QStringLiteral("all")
                     : playlist.repeat() == PlaylistModel::RepeatOne ? QStringLiteral("one")
                                                                     : QStringLiteral("off"));

        // SPEC.md §Settings: the preset name is recorded, but the band values
        // are what is authoritative on restore — a preset may have been edited,
        // or its definition may have changed since it was chosen.
        Equaliser *eq = engine.equaliser();
        QVariantList bands;
        for (double gain : eq->bands())
            bands.append(gain);
        out.setValue(QStringLiteral("equaliser/enabled"), eq->isEnabled());
        out.setValue(QStringLiteral("equaliser/preamp"), eq->preamp());
        out.setValue(QStringLiteral("equaliser/bands"), bands);
        out.setValue(QStringLiteral("equaliser/preset"), eq->preset());
    });

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty(QStringLiteral("Engine"), &engine);
    qml.rootContext()->setContextProperty(QStringLiteral("Playlist"), &playlist);
    qml.rootContext()->setContextProperty(QStringLiteral("PlaylistView"), &view);
    qml.rootContext()->setContextProperty(QStringLiteral("Equaliser"), equaliser);

    // Phase 2 command line stays minimal; the --enqueue / --play / --replace
    // forms in F-052 arrive with single-instance handling in Phase 6.
    QList<QUrl> arguments;
    for (const QString &argument : app.arguments().mid(1))
        arguments.append(QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath()));
    if (!arguments.isEmpty()) {
        playlist.addPaths(arguments);
        if (playlist.rowCount() > 0)
            playlist.setCurrentRow(0);
    }

    qml.load(QUrl(QStringLiteral("qrc:/qt/qml/Ferrolux/qml/Main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;

    const int status = app.exec();
    gst_deinit();
    return status;
}
