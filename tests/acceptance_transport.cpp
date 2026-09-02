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

using ferrolux::core::Engine;

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

    runStopStartCycles(files.first(), 20);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
