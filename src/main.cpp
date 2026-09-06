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
// Settings persistence lives here rather than in core/ so that core/ keeps no
// dependency on the desktop; platform/Settings takes it over later in Phase 6.
// Keys and defaults are those in SPEC.md §Settings and are never invented
// locally.

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
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
#include "core/Equaliser.h"
#include "library/PlaylistModel.h"
#include "meters/FrameTimer.h"
#include "meters/MeterTexture.h"
#include "ui/ThemeTokens.h"
#include "ui/VisualSettings.h"

using ferrolux::app::Player;
using ferrolux::core::Equaliser;
using ferrolux::library::PlaylistModel;
using ferrolux::meters::FrameTimer;
using ferrolux::meters::MeterSource;
using ferrolux::meters::MeterTexture;
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
        auto &engine = *player.engine();
        auto &playlist = *player.playlist();
        auto &meters = *player.meters();

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

            // Last, and only as a label. The curve is already loaded; this asks
            // the equaliser whether that curve is still the preset it was saved
            // under, and takes the name only if it is. Written on exit and never
            // read, the key left the panel reporting `flat` over somebody else's
            // curve — which nobody noticed until the panel had a lit field to
            // show it in. See BUG-019.
            equaliser->adoptPreset(settings.value(QStringLiteral("equaliser/preset")).toString());
        }

        meters.setMode(settings.value(QStringLiteral("meters/mode"),
                                      QStringLiteral("spectrum")).toString());
        meters.setReferenceLevel(
            settings.value(QStringLiteral("meters/reference-level"),
                           MeterSource::kDefaultReferenceDb).toDouble());
        meters.setBandCount(settings.value(QStringLiteral("meters/bands"),
                                           MeterSource::kSpectrumBands).toInt());

        // The proportions of the displays, as distinct from the colours of the
        // panel. A finish cannot reach these and they survive a change of one:
        // a preference for a coarse ladder is not a preference about paint.
        VisualSettings visuals;
        visuals.load();

        QObject::connect(&app, &QGuiApplication::aboutToQuit, &visuals, [&visuals] {
            visuals.save();
        });

        QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, [&engine, &playlist, &meters] {
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
            out.setValue(QStringLiteral("meters/mode"), meters.mode());
            out.setValue(QStringLiteral("meters/reference-level"), meters.referenceLevel());
            out.setValue(QStringLiteral("meters/bands"), meters.bandCount());
        });

        // The texture item is instantiated from QML so it joins the scene graph and
    // can be named as a ShaderEffect source.
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

        // The token set named by `ui/theme`, per SPEC.md §Settings and F-044.
        // Refusing to start on a set that will not load at all is deliberate: a
        // half-loaded set draws the chassis in whatever a missing colour
        // resolves to, which is black, and black chrome under a black readout
        // is not a visible failure. A *name* that no longer resolves is a
        // different thing — a stale setting — and loadNamed falls back for it.
        ThemeTokens theme;
        if (!theme.loadNamed(settings.value(QStringLiteral("ui/theme"),
                                            ThemeTokens::defaultName()).toString())) {
            qCritical("%s", qPrintable(theme.lastError()));
            return 1;
        }

    QQmlApplicationEngine qml;
        qml.rootContext()->setContextProperty(QStringLiteral("Engine"), &engine);
        qml.rootContext()->setContextProperty(QStringLiteral("Playlist"), &playlist);
        qml.rootContext()->setContextProperty(QStringLiteral("PlaylistView"), player.view());
        qml.rootContext()->setContextProperty(QStringLiteral("Equaliser"), equaliser);
        qml.rootContext()->setContextProperty(QStringLiteral("Meters"), &meters);
        qml.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
        qml.rootContext()->setContextProperty(QStringLiteral("Visuals"), &visuals);

        // A path on the command line fills the playlist and selects the first
        // track, but does not start it. Handing over a directory of several
        // hundred files and having audio begin unbidden is a surprise, and
        // F-052's explicit --play form only means something if the bare default
        // is not that. See BUG-015.
        //
        // The --enqueue / --play / --replace forms themselves arrive with
        // single-instance handling in Phase 6.
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

            // F-042. Set directly rather than through setCompact(), which
            // animates the window down from whatever it was: at startup there
            // is nothing to come down from, and the height the panel would be
            // told to return to would be the default rather than the one the
            // user last had. SPEC.md §Settings owns this key.
            if (settings.value(QStringLiteral("ui/compact"), false).toBool())
                window->setProperty("compact", true);

            QObject::connect(&app, &QGuiApplication::aboutToQuit, window, [window, &theme] {
                QSettings out;
                out.setValue(QStringLiteral("ui/compact"), window->property("compact"));
                out.setValue(QStringLiteral("ui/theme"), theme.name());
                out.setValue(QStringLiteral("ui/display-inverted"),
                             window->property("displayInverted"));
            });

            window->setProperty("displayInverted",
                                settings.value(QStringLiteral("ui/display-inverted"), false).toBool());

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
