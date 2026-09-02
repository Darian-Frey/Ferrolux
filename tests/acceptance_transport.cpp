// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 1 acceptance harness.
//
// Drives core/Engine through the criteria in ROADMAP.md Phase 1 without a
// display: a FLAC and a VBR MP3 play end to end, seek accurately, pause and
// resume cleanly, and survive twenty stop-start cycles without leaking or
// hanging the pipeline. Position is advanced by calling poll() inside the
// spin loop, standing in for the per-frame poll the QML harness does.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>
#include <unistd.h>

#include <cstdio>
#include <functional>

#include "core/Engine.h"
#include "core/Equaliser.h"
#include "library/PlaylistModel.h"

using ferrolux::core::Engine;
using ferrolux::library::PlaylistModel;

namespace {

int failures = 0;

constexpr qint64 kSecond = 1'000'000'000;
constexpr qint64 kSeekToleranceNs = 500 * 1'000'000; // F-003

void check(bool ok, const char *what, const QString &detail = {})
{
    std::printf("  [%s] %s%s%s\n", ok ? "pass" : "FAIL", what,
                detail.isEmpty() ? "" : " — ", detail.isEmpty() ? "" : qPrintable(detail));
    std::fflush(stdout);
    if (!ok)
        ++failures;
}

bool spin(Engine &engine, const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        engine.poll();
        if (done())
            return true;
    } while (timer.elapsed() < timeoutMs);
    return done();
}

long residentKb()
{
    FILE *file = std::fopen("/proc/self/statm", "r");
    if (!file)
        return -1;
    long total = 0;
    long resident = 0;
    if (std::fscanf(file, "%ld %ld", &total, &resident) != 2)
        resident = -1;
    std::fclose(file);
    return resident < 0 ? -1 : resident * (sysconf(_SC_PAGESIZE) / 1024);
}

QString ms(qint64 nanoseconds)
{
    return QStringLiteral("%1 ms").arg(nanoseconds / 1'000'000);
}

// Gain laws, tested as pure functions. These need no pipeline, and they exist
// as a separate check because BUG-003 — a mono panning law specified for a
// stereo balance control — survived the first acceptance run purely because
// nothing exercised balance at all.
void runGainLaws()
{
    std::printf("\ngain laws\n");

    // SPEC.md §Volume taper: a = v³.
    bool cubic = true;
    const double volumes[][2] = { {0.0, 0.0}, {0.25, 0.015625}, {0.5, 0.125},
                                  {0.7, 0.343}, {1.0, 1.0} };
    for (const auto &pair : volumes)
        cubic = cubic && qAbs(Engine::volumeAmplitude(pair[0]) - pair[1]) < 1e-9;
    check(cubic, "volume taper is cubic");
    check(Engine::volumeAmplitude(-5.0) == 0.0 && Engine::volumeAmplitude(5.0) == 1.0,
          "volume taper clamps out-of-range input");

    double left = 0.0;
    double right = 0.0;

    Engine::balanceGains(0.0, left, right);
    check(left == 1.0 && right == 1.0, "balance is unity at centre");

    Engine::balanceGains(-1.0, left, right);
    check(left == 1.0 && right == 0.0, "hard left silences the right channel");

    Engine::balanceGains(1.0, left, right);
    check(left == 0.0 && right == 1.0, "hard right silences the left channel");

    bool withinUnity = true;
    bool nearChannelHeld = true;
    bool monotonic = true;
    double previousRight = -1.0;
    for (int step = -100; step <= 100; ++step) {
        const double b = step / 100.0;
        Engine::balanceGains(b, left, right);

        if (left > 1.0 || right > 1.0 || left < 0.0 || right < 0.0)
            withinUnity = false;
        // Whichever channel is being favoured must sit at unity, not above it.
        if (b <= 0.0 && qAbs(left - 1.0) > 1e-12)
            nearChannelHeld = false;
        if (b >= 0.0 && qAbs(right - 1.0) > 1e-12)
            nearChannelHeld = false;
        if (right < previousRight)
            monotonic = false;
        previousRight = right;
    }
    check(withinUnity, "no balance position exceeds unity gain (AV-003)");
    check(nearChannelHeld, "the favoured channel holds unity across the range");
    check(monotonic, "right-channel gain rises monotonically left to right");
}

void runFile(const QString &path)
{
    const QFileInfo info(path);
    std::printf("\n%s\n", qPrintable(info.fileName()));

    Engine engine;
    bool sawEndOfStream = false;
    QObject::connect(&engine, &Engine::endOfStream, [&] { sawEndOfStream = true; });

    engine.setSource(QUrl::fromLocalFile(info.absoluteFilePath()));
    const bool prerolled = spin(engine, [&] { return engine.state() != Engine::Loading; }, 10000);
    check(prerolled && engine.state() == Engine::Paused, "prerolls to Paused",
          engine.errorText());
    if (engine.state() == Engine::Error)
        return;

    engine.play();
    const bool advancing = spin(engine, [&] { return engine.position() > kSecond / 2; }, 5000);
    check(advancing, "position advances while playing", ms(engine.position()));
    check(engine.state() == Engine::Playing, "state is Playing");

    // A VBR MP3 without a Xing header cannot answer a duration query at preroll;
    // it becomes answerable once enough of the stream has been seen. Waiting is
    // correct behaviour to verify, not a workaround.
    const bool haveDuration = spin(engine, [&] { return engine.duration() > 0; }, 5000);
    const qint64 duration = engine.duration();
    check(haveDuration && duration > 30 * kSecond && duration < 35 * kSecond,
          "duration reported", ms(duration));
    check(engine.isSeekable(), "reports seekable");

    // F-003: a VBR seek must land within 500 ms of the target. Measured with the
    // pipeline paused, so that playback during the settle window cannot be
    // mistaken for seek error.
    engine.pause();
    spin(engine, [] { return false; }, 200);
    engine.seek(20 * kSecond);
    spin(engine, [] { return false; }, 500);
    const qint64 drift = qAbs(engine.position() - 20 * kSecond);
    check(drift < kSeekToleranceNs, "seek lands within 500 ms", ms(drift));

    // F-002: pause preserves position. Held value is read after the pause has
    // settled, not during the transition.
    engine.play();
    spin(engine, [&] { return engine.state() == Engine::Playing; }, 3000);
    spin(engine, [] { return false; }, 300);
    engine.pause();
    spin(engine, [] { return false; }, 200);
    const qint64 held = engine.position();
    spin(engine, [] { return false; }, 400);
    check(engine.state() == Engine::Paused, "state is Paused");
    check(engine.position() == held, "pause preserves position", ms(engine.position()));

    engine.play();
    check(spin(engine, [&] { return engine.position() > held; }, 3000),
          "resumes from the held position");

    // F-002: stop resets position to zero.
    engine.stop();
    check(engine.state() == Engine::Stopped, "state is Stopped");
    check(engine.position() == 0, "stop resets position to zero");

    // End to end. Seeks close to the end so the run stays short; full-length
    // playback is covered by the separate end-to-end pass.
    engine.play();
    spin(engine, [&] { return engine.state() == Engine::Playing; }, 5000);
    engine.seek(engine.duration() - 2 * kSecond);
    check(spin(engine, [&] { return sawEndOfStream; }, 8000), "reaches end of stream");
    check(engine.state() == Engine::Stopped, "returns to Stopped after EOS");
}

// F-005. The audible half of gapless — "no discontinuity at the join" — needs a
// known-gapless reference recording and a capture of the sink, which AV-006
// describes and this harness does not attempt. What is verified here is the
// mechanism: that the handover happens through about-to-finish without the
// pipeline ever returning to Stopped, and that the playlist follows it without
// restarting playback.
// BUG-007. The filter chain must run in floating point: left to negotiate
// freely against a 16-bit source it settles on S16LE, and the equaliser then
// runs ten cascaded IIR biquads in 16-bit integer, rounding after every
// section. That is audibly gritty on real music and completely silent in a
// bypass test, because at unity gain an S16 round trip is still bit-exact.
//
// Checked against the engine's own pipeline rather than a chain assembled by
// the test, because the chain the test assembles is exactly what missed it.
void runPipelineFormat(const QString &path)
{
    std::printf("\nfilter chain format (BUG-007)\n");

    Engine engine;
    engine.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    engine.play();
    if (!spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000)) {
        check(false, "pipeline reaches Playing so caps are negotiated");
        return;
    }

    GstElement *filter = engine.equaliser()->filterElement();
    if (!filter) {
        check(false, "the equaliser element exists");
        return;
    }

    QString format;
    if (GstPad *pad = gst_element_get_static_pad(filter, "sink")) {
        if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
            if (GstStructure *structure = gst_caps_get_structure(caps, 0)) {
                if (const gchar *value = gst_structure_get_string(structure, "format"))
                    format = QString::fromUtf8(value);
            }
            gst_caps_unref(caps);
        }
        gst_object_unref(pad);
    }

    check(!format.isEmpty(), "the equaliser has negotiated caps", format);
    check(format.startsWith(QLatin1Char('F')),
          "the equaliser runs in floating point, not integer", format);

    engine.stop();
}

void runGapless(const QString &first, const QString &second)
{
    std::printf("\ngapless handover (F-005)\n");

    Engine engine;
    PlaylistModel playlist;

    bool sawHandover = false;
    bool sawStopped = false;
    int sourceLoads = 0;

    QObject::connect(&engine, &Engine::stateChanged, [&] {
        if (engine.state() == Engine::Stopped)
            sawStopped = true;
    });
    QObject::connect(&playlist, &PlaylistModel::currentEntryChanged,
                     [&](const QUrl &url) {
                         if (url.isEmpty())
                             return;
                         ++sourceLoads;
                         engine.setSource(url);
                         engine.play();
                     });
    QObject::connect(&playlist, &PlaylistModel::nextEntryChanged,
                     &engine, &Engine::setNextSource);
    QObject::connect(&engine, &Engine::gaplessAdvance, [&] {
        sawHandover = true;
        playlist.advanceForHandover();
    });

    const QUrl a = QUrl::fromLocalFile(QFileInfo(first).absoluteFilePath());
    const QUrl b = QUrl::fromLocalFile(QFileInfo(second).absoluteFilePath());
    playlist.addUrls({ a, b });
    playlist.setCurrentRow(0);

    check(spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000),
          "first track reaches Playing");
    check(spin(engine, [&] { return engine.duration() > 0; }, 5000),
          "duration known before the join");

    // Seek close to the end so about-to-finish fires within the test's patience.
    engine.seek(engine.duration() - 2 * kSecond);
    spin(engine, [] { return false; }, 300);

    sawStopped = false;             // only the join itself is under test
    const int loadsBefore = sourceLoads;

    check(spin(engine, [&] { return sawHandover; }, 12000),
          "about-to-finish handed over to the next track");
    check(!sawStopped, "the pipeline never returned to Stopped across the join");
    check(engine.state() == Engine::Playing, "still Playing after the join");
    check(playlist.currentRow() == 1, "the playlist advanced its cursor",
          QStringLiteral("row %1").arg(playlist.currentRow()));
    check(sourceLoads == loadsBefore,
          "the handover did not re-load the source, which would have caused a gap");
    check(engine.source() == b, "engine reports the track it handed over to",
          engine.source().fileName());

    // The last entry has no successor, so the handover must disarm rather than
    // loop or stall.
    spin(engine, [] { return false; }, 200);
    check(playlist.nextRow() == -1, "no next entry at the end of the list");
}

void runStopStartCycles(const QString &path, int cycles)
{
    std::printf("\n%d stop-start cycles\n", cycles);

    Engine engine;
    engine.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    spin(engine, [&] { return engine.state() != Engine::Loading; }, 10000);

    const long before = residentKb();
    bool allReachedPlaying = true;

    for (int i = 0; i < cycles; ++i) {
        engine.play();
        if (!spin(engine, [&] { return engine.state() == Engine::Playing; }, 5000))
            allReachedPlaying = false;
        spin(engine, [] { return false; }, 60);
        engine.stop();
        if (!spin(engine, [&] { return engine.state() == Engine::Stopped; }, 5000))
            allReachedPlaying = false;
    }

    const long after = residentKb();
    check(allReachedPlaying, "every cycle reached Playing and returned to Stopped");
    check(engine.state() == Engine::Stopped, "pipeline is not hung");
    check(after - before < 8192, "resident memory growth bounded",
          QStringLiteral("%1 kB -> %2 kB (%3 kB)").arg(before).arg(after).arg(after - before));
}

} // namespace

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    const QStringList files = app.arguments().mid(1);
    if (files.size() < 2) {
        std::fprintf(stderr, "usage: acceptance_transport <flac> <vbr-mp3>\n");
        return 2;
    }

    runGainLaws();

    for (const QString &file : files)
        runFile(file);

    runPipelineFormat(files.first());

    runGapless(files.first(), files.at(1));

    runStopStartCycles(files.first(), 20);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
