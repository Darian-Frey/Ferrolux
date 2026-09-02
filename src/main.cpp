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
#include "library/MetadataReader.h"
#include "library/PlaylistFilter.h"
#include "library/PlaylistModel.h"

using ferrolux::core::Engine;
using ferrolux::library::MetadataReader;
using ferrolux::library::PlaylistFilter;
using ferrolux::library::PlaylistModel;

namespace {

// Expands directories recursively and keeps only things that look like audio.
// Suffix matching rather than content sniffing: sniffing twenty thousand files
// to decide whether to list them would defeat the point of listing them at all.
QList<QUrl> collectAudio(const QList<QUrl> &inputs)
{
    static const QStringList suffixes = {
        QStringLiteral("flac"), QStringLiteral("mp3"),  QStringLiteral("ogg"),
        QStringLiteral("oga"),  QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("aac"),  QStringLiteral("wav"),  QStringLiteral("aiff"),
        QStringLiteral("aif"),  QStringLiteral("wv"),   QStringLiteral("mpc"),
        QStringLiteral("alac"),
    };

    QList<QUrl> found;
    for (const QUrl &input : inputs) {
        if (!input.isLocalFile()) {
            found.append(input);
            continue;
        }

        const QString path = input.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = it.next();
                if (suffixes.contains(QFileInfo(candidate).suffix().toLower()))
                    found.append(QUrl::fromLocalFile(candidate));
            }
        } else if (suffixes.contains(info.suffix().toLower())) {
            found.append(QUrl::fromLocalFile(info.absoluteFilePath()));
        }
    }
    std::sort(found.begin(), found.end(),
              [](const QUrl &a, const QUrl &b) { return a.toString() < b.toString(); });
    return found;
}

} // namespace

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

    QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, [&engine, &playlist] {
        QSettings out;
        out.setValue(QStringLiteral("playback/volume"), engine.volume());
        out.setValue(QStringLiteral("playback/balance"), engine.balance());
        out.setValue(QStringLiteral("playback/shuffle"), playlist.shuffle());
        out.setValue(QStringLiteral("playback/repeat"),
                     playlist.repeat() == PlaylistModel::RepeatAll   ? QStringLiteral("all")
                     : playlist.repeat() == PlaylistModel::RepeatOne ? QStringLiteral("one")
                                                                     : QStringLiteral("off"));
    });

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty(QStringLiteral("Engine"), &engine);
    qml.rootContext()->setContextProperty(QStringLiteral("Playlist"), &playlist);
    qml.rootContext()->setContextProperty(QStringLiteral("PlaylistView"), &view);

    // Phase 2 command line stays minimal; the --enqueue / --play / --replace
    // forms in F-052 arrive with single-instance handling in Phase 6.
    QList<QUrl> arguments;
    for (const QString &argument : app.arguments().mid(1))
        arguments.append(QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath()));
    const QList<QUrl> initial = collectAudio(arguments);
    if (!initial.isEmpty()) {
        playlist.addUrls(initial);
        playlist.setCurrentRow(0);
    }

    qml.load(QUrl(QStringLiteral("qrc:/qt/qml/Ferrolux/qml/Main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;

    const int status = app.exec();
    gst_deinit();
    return status;
}
