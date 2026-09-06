// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Ferrolux RS-1 — entry point.
//
// Starts the process and hands `app/Player` to the panel.
//
// The arrows between the engine, the playlist, the metadata reader and the
// meters used to be a screenful of lambdas here. They are `Player` now, because
// Phase 6 adds four more consumers of the same objects and this function would
// have stopped being readable well before the last of them landed — IMP-005.
// What is left is what genuinely belongs to starting a process: GStreamer and
// Qt initialisation, the fonts, the token set, the QML context, the command
// line, and AV-002's measurement mode.
//
// Settings persistence is `platform/Settings`, which owns every key in SPEC.md
// §Settings. It is constructed here and asked to restore and to save; what it
// remembers and in what order is its business, not this function's.

#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <QUrl>

#include <algorithm>

#include <gst/gst.h>

#include "app/Player.h"
#include "platform/Settings.h"
#include "core/Equaliser.h"
#include "library/PlaylistModel.h"
#include "meters/FrameTimer.h"
#include "meters/MeterTexture.h"
#include "ui/ThemeTokens.h"
#include "ui/VisualSettings.h"

using ferrolux::app::Player;
using ferrolux::meters::FrameTimer;
using ferrolux::meters::MeterTexture;
using ferrolux::platform::Settings;
using ferrolux::ui::ThemeTokens;
using ferrolux::ui::VisualSettings;

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    // Measurement mode for AV-002. With the swap interval left alone, frames
    // arrive at the refresh rate whether they cost a millisecond or fifteen,
    // so the interval says nothing about headroom. Disabling it lets frames run
    // as fast as they can be produced, and the interval becomes the true cost
    // of one. Only ever set by tools/measure-frames.sh.
    //
    // The variable holds the number of seconds to measure for, and the run ends
    // itself. Killing it from outside would be simpler but SIGTERM does not
    // reach aboutToQuit, so the report would never be written.
    const int measureSeconds = qEnvironmentVariableIntValue("FERROLUX_FRAME_MEASURE");
    const bool measuring = measureSeconds > 0;
    if (measuring) {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSwapInterval(0);
        QSurfaceFormat::setDefaultFormat(format);
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Everything that owns a GStreamer object lives inside this scope, so that
    // all of it is destroyed before gst_deinit() runs. Calling gst_deinit()
    // while a pipeline is still alive deadlocks — it waits for a teardown that
    // cannot happen until the owner is destroyed, and the owner is not
    // destroyed until main returns. That is BUG-012: the window closed, the
    // event loop exited, and the process then hung for ever with the audio
    // still going.
    int status = 0;
    {
        // Every object the application is made of, and every arrow between
        // them. Declared inside this scope so that all of it is destroyed
        // before gst_deinit() below.
        Player player;
        VisualSettings visuals;
        ThemeTokens theme;

        // Refusing to start on a token set that will not load at all is
        // deliberate: a half-loaded set draws the chassis in whatever a missing
        // colour resolves to, which is black, and black chrome under a black
        // readout is not a visible failure.
        Settings persisted(&player, &visuals, &theme);
        if (!persisted.restore()) {
            qCritical("%s", qPrintable(persisted.lastError()));
            return 1;
        }
        QObject::connect(&app, &QGuiApplication::aboutToQuit,
                         &persisted, &Settings::save);

        // The texture item is instantiated from QML so it joins the scene graph
        // and can be named as a ShaderEffect source.
        qmlRegisterType<MeterTexture>("Ferrolux", 1, 0, "MeterTexture");

        // The panel's four faces, from the binary rather than from the system
        // (D-012, SPEC.md §Typography). A missing face does not fail: Qt
        // substitutes a system font, and a substitution in the middle of a lit
        // readout is a defect that has to be seen to be found. So each one is
        // checked here, where it can still be said out loud.
        for (const QString &face : { QStringLiteral(":/resources/fonts/DSEG7Classic-Regular.ttf"),
                                     QStringLiteral(":/resources/fonts/DSEG14Classic-Regular.ttf"),
                                     QStringLiteral(":/resources/fonts/Handjet-Panel.ttf"),
                                     QStringLiteral(":/resources/fonts/IBMPlexSansCondensed-Regular.ttf") }) {
            if (QFontDatabase::addApplicationFont(face) < 0)
                qWarning("could not load %s; the panel will substitute a system face",
                         qPrintable(face));
        }

        QQmlApplicationEngine qml;
        qml.rootContext()->setContextProperty(QStringLiteral("Engine"), player.engine());
        qml.rootContext()->setContextProperty(QStringLiteral("Playlist"), player.playlist());
        qml.rootContext()->setContextProperty(QStringLiteral("PlaylistView"), player.view());
        qml.rootContext()->setContextProperty(QStringLiteral("Equaliser"), player.equaliser());
        qml.rootContext()->setContextProperty(QStringLiteral("Meters"), player.meters());
        qml.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
        qml.rootContext()->setContextProperty(QStringLiteral("Visuals"), &visuals);

        // Paths fill the playlist and select the first without starting it;
        // `Player::Open` is where that rule and its reasons live. The
        // --enqueue / --play / --replace forms arrive with single-instance
        // handling later in this phase (F-052).
        QList<QUrl> arguments;
        for (const QString &argument : app.arguments().mid(1))
            arguments.append(QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath()));
        player.open(arguments, Player::AddAndSelect);

        qml.load(QUrl(QStringLiteral("qrc:/qt/qml/Ferrolux/qml/Main.qml")));
        if (qml.rootObjects().isEmpty())
            return 1;

        // AV-002. Attached to the window rather than to the meters, because the
        // meters share their frame with everything else drawn in it.
        FrameTimer frameTimer;
        if (auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first())) {
            frameTimer.attach(window);

            // Everything the panel itself remembers. It could not be applied
            // any earlier than this: QML builds the window, so there was
            // nothing to apply it to until `qml.load` returned.
            persisted.attachWindow(window);

            if (measuring) {
                const QString geometry = qEnvironmentVariable("FERROLUX_GEOMETRY");
                const QStringList parts = geometry.split(QLatin1Char('x'));
                if (parts.size() == 2)
                    window->resize(parts.at(0).toInt(), parts.at(1).toInt());

                // The window manager clamps this to the screen, so a request
                // larger than the display is silently reduced. That is why the
                // report carries the size actually rendered rather than the one
                // asked for: a measurement that cannot say what it measured is
                // worse than none, because it will be believed.
                //
                // Neither obvious way round it works. Bypassing the window
                // manager for a larger surface leaves the window entirely
                // off-screen, where it receives no frame callbacks and renders
                // nothing. Rendering offscreen sidesteps the manager, but the
                // offscreen platform loads the software backend, which does not
                // execute the shaders at all. Measuring beyond the display needs
                // a QQuickRenderControl harness on the OpenGL RHI, which does
                // not exist yet.

                // Discard the first second: the opening frames include shader
                // compilation, texture creation and the initial layout, none of
                // which happens again and all of which would distort the average.
                QTimer::singleShot(1000, &frameTimer, [&frameTimer] { frameTimer.reset(); });

                QTimer::singleShot((measureSeconds + 1) * 1000, &frameTimer,
                                   [&frameTimer, &app, window] {
                                       // The size actually rendered, not the one
                                       // requested. A measurement that cannot say
                                       // what it measured is not a measurement.
                                       std::fprintf(stderr, "FRAMES size=%dx%d %s\n",
                                                    window->width(), window->height(),
                                                    qPrintable(frameTimer.summary()));
                                       std::fflush(stderr);
                                       app.quit();
                                   });
            }
        }

        status = app.exec();
    }

    // Safe now: every pipeline, element and worker above has been destroyed.
    gst_deinit();
    return status;
}
