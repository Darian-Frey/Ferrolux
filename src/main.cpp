// Ferrolux RS-1 — entry point.
//
// Phase 1 wires the engine to a throwaway QML harness. Settings persistence is
// done here rather than in Engine so that core/ keeps no dependency on the
// desktop; platform/Settings takes this over in Phase 6. Keys and defaults are
// those in SPEC.md §Settings and must not be invented locally.

#include <QGuiApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QUrl>

#include <gst/gst.h>

#include "core/Engine.h"

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    ferrolux::core::Engine engine;

    QSettings settings;
    engine.setVolume(settings.value(QStringLiteral("playback/volume"), 0.7).toDouble());
    engine.setBalance(settings.value(QStringLiteral("playback/balance"), 0.0).toDouble());

    QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, [&engine] {
        QSettings out;
        out.setValue(QStringLiteral("playback/volume"), engine.volume());
        out.setValue(QStringLiteral("playback/balance"), engine.balance());
    });

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty(QStringLiteral("Engine"), &engine);

    // Phase 1 command line is deliberately minimal; the --enqueue / --play /
    // --replace forms in F-052 arrive with single-instance handling in Phase 6.
    const QStringList arguments = app.arguments().mid(1);
    if (!arguments.isEmpty()) {
        const QFileInfo file(arguments.first());
        if (file.exists())
            engine.setSource(QUrl::fromLocalFile(file.absoluteFilePath()));
        else
            qWarning("No such file: %s", qPrintable(arguments.first()));
    }

    qml.load(QUrl(QStringLiteral("qrc:/qt/qml/Ferrolux/qml/Main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;

    const int status = app.exec();
    gst_deinit();
    return status;
}
