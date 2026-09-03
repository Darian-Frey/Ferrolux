// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// tests/frame_bench — the other half of AV-002's detection.
//
// tools/measure-frames.sh measures the real application in a real window, which
// is the honest thing to do and answers "does it hold 60 fps here". It cannot
// answer the two clauses Phase 4 actually sets, for two reasons that are
// properties of the desktop rather than of the code:
//
//   resolution  the window manager clamps a managed window to the screen, so a
//               3840x2160 request on a 1920-wide display renders 1920x1008 and
//               reports a 4K pass it never performed.
//   headroom    the compositor paces frames whatever the swap interval says, so
//               the interval between frames saturates at the refresh rate. A
//               display with vast headroom and one with none both read 16.7 ms.
//
// Rendering through QQuickRenderControl removes both at once. There is no
// window, so nothing clamps the size; there is no swap and no compositor, so
// nothing paces the loop, and the interval between frames becomes the actual
// cost of producing one. That is what the 30% headroom clause needs.
//
// Two things make the figure trustworthy rather than merely large:
//
//   glFinish  the RHI submits GPU work asynchronously, so render() returns long
//             before the GPU has finished. Without a fence the loop would time
//             the submission and report a shader of any cost as free. This is
//             the single most important line in the file.
//   the real  the shaders are not copied here. MeterDisplay.qml is the file the
//   display   application itself instantiates, so a change to a shader changes
//             what this measures. A benchmark over a copy measures the copy.
//
// What it deliberately does not measure: the rest of the window. The playlist,
// the chrome and the per-frame bindings share the meters' frame in the real
// application, and AV-002 names them as compounding risks — but they cost the
// same at any size, whereas the shaders are the part whose cost scales with
// pixels. So this isolates the part the resolution clause is about, and
// measure-frames.sh keeps the whole-window number. Neither replaces the other.

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlContext>
#include <QStringList>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QThread>
#include <QFontDatabase>
#include <QImage>

#include <cstdio>
#include <cmath>

#include "meters/FrameTimer.h"
#include "meters/MeterSource.h"
#include "meters/MeterTexture.h"
#include "ui/ThemeTokens.h"

using ferrolux::meters::FrameTimer;
using ferrolux::meters::MeterSource;
using ferrolux::meters::MeterTexture;
using ferrolux::ui::ThemeTokens;

namespace {

constexpr int kAnalysisBins = 512;
constexpr int kSampleRate = 44100;
constexpr double kFrameMs = 1000.0 / 60.0;

// Synthetic programme material. Two things it has to do.
//
// It has to *move*, and move differently every frame. A constant signal would
// let the smoothing settle, the texture stop changing and the upload be
// skipped, and the benchmark would then be measuring a display that never
// updates. So this is a sweep across the bands, drifting every frame.
//
// It also has to pass through *every* level, quiet included, because for the
// flame shader quiet is the expensive case and not the cheap one. A tall
// silhouette lets a pixel below the nearest crest finish after one rank; near
// silence nothing covers anything, no rank can stop early, and every pixel
// low enough to be inside a rank's reach pays for all of them. Measuring a
// loud signal alone would report the best case as though it were the average.
// The envelope below takes the material from silence to full scale and back
// several times over a run, so the mean covers the range the shader will meet.
void feed(MeterSource &meters, int frame)
{
    const double phase = double(frame) * 0.031;

    // Silence to full scale and back, roughly three times over a 600-frame run.
    const double envelope = 0.5 - 0.5 * std::cos(double(frame) * 0.0314);
    const double gain = -72.0 * (1.0 - envelope);

    QList<float> magnitudes;
    magnitudes.reserve(kAnalysisBins);
    for (int bin = 0; bin < kAnalysisBins; ++bin) {
        const double position = double(bin) / kAnalysisBins;
        const double sweep = std::sin(phase + position * 9.0);
        const double tilt = -12.0 - position * 34.0;
        magnitudes.append(float(tilt + sweep * 16.0 + gain));
    }
    meters.consumeSpectrum(magnitudes, kSampleRate);

    const double left = -20.0 + 16.0 * std::fabs(std::sin(phase)) + gain;
    const double right = -20.0 + 16.0 * std::fabs(std::sin(phase + 0.7)) + gain;
    meters.consumeLevel({ left, right }, { left + 3.0, right + 3.0 }, { left, right });

    meters.advance(kFrameMs);
}

} // namespace

int main(int argc, char *argv[])
{
    // Before QGuiApplication: the RHI backend is chosen once, and the whole
    // point of this harness is that it runs the OpenGL path the application
    // runs. Left to itself an offscreen context can land on the software
    // rasteriser, which does not execute the shaders at all and would report a
    // comfortable pass for a display that had never been drawn.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    const QStringList arguments = app.arguments();
    const int width = arguments.size() > 1 ? arguments.at(1).toInt() : 3840;
    const int height = arguments.size() > 2 ? arguments.at(2).toInt() : 2160;
    const int frameCount = arguments.size() > 3 ? arguments.at(3).toInt() : 600;
    const QString only = arguments.size() > 4 ? arguments.at(4) : QString();
    const QSize size(width, height);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapInterval(0);

    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create()) {
        std::fprintf(stderr, "no OpenGL context; this needs a GPU and a display connection\n");
        return 2;
    }

    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) {
        std::fprintf(stderr, "could not make the offscreen surface current\n");
        return 2;
    }

    qmlRegisterType<MeterTexture>("Ferrolux", 1, 0, "MeterTexture");

    // The display is a piece of the real panel and resolves real tokens, so the
    // bench has to supply them. It did not, once: tokenising the mode label
    // gave MeterDisplay.qml a dependency on the Tokens singleton, and this
    // program — which loads that file deliberately, so that it measures the
    // shaders the application shows — could not resolve it. Every run failed
    // with a page of "Unable to assign [undefined]" and no frames. The coupling
    // is the point of the design and the cost of it is this block.
    qmlRegisterSingletonType(
        QUrl::fromLocalFile(QStringLiteral(FERROLUX_QML_DIR "/Tokens.qml")),
        "Ferrolux", 1, 0, "Tokens");

    for (const QString &face : { QStringLiteral("DSEG7Classic-Regular.ttf"),
                                 QStringLiteral("DSEG14Classic-Regular.ttf"),
                                 QStringLiteral("Handjet-Panel.ttf"),
                                 QStringLiteral("IBMPlexSansCondensed-Regular.ttf") }) {
        QFontDatabase::addApplicationFont(
            QStringLiteral(FERROLUX_RESOURCE_DIR "/fonts/") + face);
    }

    ThemeTokens theme;
    if (!theme.load(QStringLiteral(FERROLUX_RESOURCE_DIR "/themes/ferric.json"))) {
        std::fprintf(stderr, "%s\n", qPrintable(theme.lastError()));
        return 2;
    }

    int failures = 0;
    bool first = true;

    for (const QString &mode : MeterSource::modes()) {
        if (!only.isEmpty() && mode != only)
            continue;

        // Let the GPU settle between modes. Six hundred frames at 2160p heats
        // it, and the mode measured next inherits the heat: run back to back,
        // the last of five reads about 40% slower than the first, which put a
        // mode below the headroom floor and looked exactly like a regression
        // until the same run passed comfortably after ninety seconds idle.
        //
        // The settled figure is the honest one for this question. Only one mode
        // is ever displayed at a time, so no user reaches the fourth mode's
        // shader with three others' work still in the pipe; measuring that way
        // reports the cost of the benchmark's own sequence rather than of the
        // shader.
        if (!first)
            QThread::msleep(4000);
        first = false;

        // A fresh control, window and engine per mode. Sharing them would let
        // one mode's compiled pipelines and warmed caches flatter the next, and
        // the point of the sweep is that each mode answers for itself.
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setGeometry(0, 0, width, height);
        window.setColor(QColor(QStringLiteral("#2C2C2A")));
        window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));

        if (!control.initialize()) {
            std::fprintf(stderr, "QQuickRenderControl would not initialise\n");
            return 2;
        }

        QOpenGLFramebufferObject fbo(size, QOpenGLFramebufferObject::CombinedDepthStencil);
        window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), size));

        MeterSource meters;
        meters.setMode(mode);

        QQmlEngine engine;

        // Drop the application directory from the import path. qt_add_qml_module
        // writes a file-based `Ferrolux` module into the build directory whose
        // qmldir says `prefer :/qt/qml/Ferrolux/`, and that resource exists only
        // inside the application binary. A bench run from the build directory
        // would find that qmldir, follow the redirect, and fail to resolve
        // whichever singleton the module currently declares — an error naming
        // the singleton, and nothing naming the import path that caused it.
        //
        // MeterTexture is registered from C++ below, so `import Ferrolux`
        // resolves to the type namespace this program actually provides.
        QStringList imports = engine.importPathList();
        imports.removeAll(QCoreApplication::applicationDirPath());
        engine.setImportPathList(imports);

        engine.rootContext()->setContextProperty(QStringLiteral("Meters"), &meters);
        engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);

        QQmlComponent component(&engine, QUrl::fromLocalFile(
                                             QStringLiteral(FERROLUX_QML_DIR "/MeterDisplay.qml")));
        QScopedPointer<QObject> created(component.create());
        auto *item = qobject_cast<QQuickItem *>(created.data());
        if (!item) {
            std::fprintf(stderr, "MeterDisplay.qml: %s\n",
                         qPrintable(component.errorString()));
            return 2;
        }
        item->setParentItem(window.contentItem());
        item->setSize(size);

        FrameTimer timer;
        timer.attach(&window);

        // The first frames compile shaders, create the texture and lay the item
        // out. None of that happens again, and all of it would distort the
        // average, so they are rendered and then discarded.
        const int warmup = 60;

        for (int frame = 0; frame < warmup + frameCount; ++frame) {
            if (frame == warmup)
                timer.reset();

            feed(meters, frame);

            control.polishItems();
            control.beginFrame();
            control.sync();
            control.render();
            control.endFrame();

            // Without this the loop times the submission of GPU work rather
            // than its execution, and an expensive shader reads as free.
            context.functions()->glFinish();
        }

        // Here the interval *is* the cost of a frame: nothing paces this loop,
        // and glFinish has already waited for the GPU. That is why headroom is
        // read from it rather than from the CPU render pass, which is all the
        // in-application measurement can offer.
        const double frameMs = timer.meanIntervalMs();
        const double headroom = timer.frameHeadroom();

        // The verdict is taken from the mean and from the count of frames that
        // ran over by half a budget, not from the single worst interval.
        //
        // This loop is unpaced but not alone: it shares the GPU with the
        // compositor and with whatever else is on the desktop, so an isolated
        // 20 ms frame is as likely to be someone else's work as this shader's,
        // and failing a run on it would make the tool report noise. A shader
        // that genuinely cannot hold the budget does not produce one late frame
        // — before this was optimised, flame at 2160p produced 377 of 600. The
        // worst interval is still printed, because a mean that passes while the
        // worst is far above it is worth looking at even when it is not a fail.
        const bool sustains = frameMs <= FrameTimer::kBudgetMs && timer.lateFrames() == 0;
        const bool spare = headroom >= 0.30;
        if (!sustains || !spare)
            ++failures;

        const char *verdict = !sustains ? "MISSED"
                              : !spare  ? "thin"
                              : timer.worstIntervalMs() > FrameTimer::kBudgetMs ? "holds*"
                                                                                : "holds";

        std::fprintf(stdout,
                     "BENCH mode=%-15s size=%dx%d frame_mean=%.3fms frame_worst=%.3fms "
                     "cpu_mean=%.3fms headroom=%.3f late=%d %s\n",
                     qPrintable(mode), width, height, frameMs, timer.worstIntervalMs(),
                     timer.meanRenderMs(), headroom, timer.lateFrames(), verdict);
        std::fflush(stdout);

        // An optimisation to a shader has to be shown not to have changed what
        // it draws, and "it looked right when I ran it" is not that. Setting
        // FERROLUX_BENCH_PNG to a directory writes the last frame of each mode,
        // so the before and after of a change can be compared as images.
        const QString dump = qEnvironmentVariable("FERROLUX_BENCH_PNG");
        if (!dump.isEmpty())
            fbo.toImage().save(dump + QLatin1Char('/') + mode + QStringLiteral(".png"));

        item->setParentItem(nullptr);
        control.invalidate();
    }

    context.doneCurrent();
    return failures == 0 ? 0 : 1;
}
