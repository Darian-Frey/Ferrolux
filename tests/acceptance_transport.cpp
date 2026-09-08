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
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>
#include <unistd.h>

#include <cstdio>
#include <functional>

#include "Check.h"
#include "app/Player.h"
#include "core/Engine.h"
#include "core/Equaliser.h"
#include "library/PlaylistModel.h"
#include "meters/MeterSource.h"

using ferrolux::app::Player;
using ferrolux::core::Engine;
using ferrolux::library::PlaylistModel;
using ferrolux::meters::MeterSource;

using ferrolux::tests::check;
using ferrolux::tests::failures;

namespace {

constexpr qint64 kSecond = 1'000'000'000;
constexpr qint64 kSeekToleranceNs = 500 * 1'000'000; // F-003

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
// F-002's remaining clauses, which needed two things that did not exist when the
// status was written. Next has no meaning without a playlist to advance through
// — that arrived with F-012 in Phase 2 — and neither next nor previous can be
// exercised against `Engine` alone, because play order belongs to the model
// (invariant 5) and only `app::Player` holds both. The note saying "next is
// wired to stop in the harness" has been stale for four phases.
void runPlayOrder(const QString &first, const QString &second)
{
    std::printf("\nplay order and the previous-track rule (F-002)\n");

    Player player;
    Engine &engine = *player.engine();
    PlaylistModel &playlist = *player.playlist();

    playlist.addPaths({ QUrl::fromLocalFile(first), QUrl::fromLocalFile(second) });
    check(playlist.rowCount() == 2, "two entries to move between",
          QStringLiteral("%1 rows").arg(playlist.rowCount()));
    if (playlist.rowCount() != 2)
        return;

    playlist.setCurrentRow(0);
    check(spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000),
          "the first entry plays");

    // ---- next ------------------------------------------------------------
    player.next();
    check(spin(engine, [&] { return playlist.currentRow() == 1; }, 5000),
          "next moves to the second entry",
          QStringLiteral("row %1").arg(playlist.currentRow()));
    check(spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000),
          "and it plays rather than merely being selected");

    // ---- previous, outside the window -------------------------------------
    // Past three seconds, previous means "start this one again" — the rule a
    // transport gets wrong by treating the key as a plain step backwards.
    engine.seek(5 * kSecond);
    check(spin(engine, [&] { return engine.position() > 4 * kSecond; }, 5000),
          "seeks past the three-second window", ms(engine.position()));

    player.previous();
    const bool restarted = spin(engine, [&] { return engine.position() < 2 * kSecond; }, 5000);
    check(restarted, "previous restarts the track when past the window",
          ms(engine.position()));
    check(playlist.currentRow() == 1,
          "and stays on the same entry",
          QStringLiteral("row %1").arg(playlist.currentRow()));

    // ---- previous, inside the window --------------------------------------
    // Within three seconds it means the entry before. The position was just
    // reset by the restart above, so this is the window without contriving one.
    player.previous();
    check(spin(engine, [&] { return playlist.currentRow() == 0; }, 5000),
          "previous inside the window moves to the entry before",
          QStringLiteral("row %1").arg(playlist.currentRow()));

    // ---- previous at the start of the list ---------------------------------
    // Nothing before the first entry. It must not wrap, and it must not stop.
    player.previous();
    spin(engine, [&] { return false; }, 1200);
    check(playlist.currentRow() == 0,
          "previous at the first entry stays there rather than wrapping",
          QStringLiteral("row %1").arg(playlist.currentRow()));
}

// The clause nobody had measured: "play, pause, stop, previous, next respond
// within 100 ms of input". What is timed is the call itself — how long the
// application takes to accept the command — rather than how long GStreamer takes
// to have audio coming out, which is a different promise and one no player can
// make from a cold pipeline. A transport that blocks is a transport that feels
// broken, and that is what this is about.
void runTransportLatency(const QString &path)
{
    std::printf("\ntransport response (F-002)\n");

    Player player;
    Engine &engine = *player.engine();
    PlaylistModel &playlist = *player.playlist();
    playlist.addPaths({ QUrl::fromLocalFile(path) });
    playlist.setCurrentRow(0);
    spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000);

    constexpr qint64 kBudgetMs = 100; // F-002

    const auto timed = [&](const char *what, const std::function<void()> &action) {
        QElapsedTimer clock;
        clock.start();
        action();
        const qint64 took = clock.elapsed();
        check(took < kBudgetMs, what, QStringLiteral("%1 ms").arg(took));
    };

    timed("pause responds within 100 ms", [&] { player.pause(); });
    timed("play responds within 100 ms", [&] { player.play(); });
    timed("next responds within 100 ms", [&] { player.next(); });
    timed("previous responds within 100 ms", [&] { player.previous(); });
    timed("stop responds within 100 ms", [&] { player.stop(); });
}

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

// F-030. The analysis elements are pass-through and post on the bus; this
// checks the whole path from a playing pipeline to display-ready meter state,
// which the pure meters_test suite deliberately does not cover.
void runMeters(const QString &path)
{
    std::printf("\nmeter acquisition (F-030)\n");

    Engine engine;
    MeterSource meters;

    int levelMessages = 0;
    int spectrumMessages = 0;
    int spectrumBins = 0;
    int levelChannels = 0;
    int sampleRate = 0;

    QObject::connect(&engine, &Engine::levelMeasured,
                     [&](const QList<double> &rms, const QList<double> &peak,
                         const QList<double> &decay) {
                         ++levelMessages;
                         levelChannels = int(rms.size());
                         meters.consumeLevel(rms, peak, decay);
                     });
    QObject::connect(&engine, &Engine::spectrumMeasured,
                     [&](const QList<float> &magnitudes, int rate) {
                         ++spectrumMessages;
                         spectrumBins = int(magnitudes.size());
                         sampleRate = rate;
                         meters.consumeSpectrum(magnitudes, rate);
                     });

    engine.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    engine.play();
    check(spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000),
          "pipeline reaches Playing");

    // Counters start from the moment the connections are made, which includes
    // the spin to Playing. Reset before measuring, or preroll traffic is
    // counted as though it arrived during the window.
    levelMessages = 0;
    spectrumMessages = 0;

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        engine.poll();
        meters.advance(5.0);
    }

    check(levelMessages > 0, "level posts on the bus",
          QStringLiteral("%1 messages in 1 s").arg(levelMessages));
    check(spectrumMessages > 0, "spectrum posts on the bus",
          QStringLiteral("%1 messages in 1 s").arg(spectrumMessages));

    // 16 ms interval, so roughly sixty a second. Wide bounds: the interval is a
    // floor, not a promise, and a loaded machine will deliver fewer.
    // A 16 ms interval is about sixty a second, but the elements run ahead of
    // the sink rather than in step with it (BUG-011), so the arrival rate is
    // bursty. Wide bounds: this checks that messages flow at a plausible rate,
    // not that they are evenly spaced.
    check(levelMessages > 20 && levelMessages < 300,
          "at a plausible rate for the configured 16 ms interval",
          QStringLiteral("%1 in one second").arg(levelMessages));

    check(spectrumBins == 512, "spectrum reports the configured 512 analysis bins",
          QString::number(spectrumBins));
    check(levelChannels == 2, "level reports both channels",
          QString::number(levelChannels));
    check(sampleRate > 0, "the sample rate is recovered from negotiated caps",
          QStringLiteral("%1 Hz").arg(sampleRate));

    // Something must actually be moving.
    float loudest = 0.0f;
    for (float value : meters.magnitudes())
        loudest = std::max(loudest, value);
    check(loudest > 0.0f, "the spectrum bands carry signal",
          QStringLiteral("loudest band %1").arg(loudest, 0, 'f', 3));
    check(meters.vuDeflection(0) > 0.0, "the VU needle has moved off its rest",
          QStringLiteral("%1").arg(meters.vuDeflection(0), 0, 'f', 4));

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

// BUG-027. A next entry that exists, is readable, and does not decode used to
// cost the track before it its last two seconds.
//
// `playbin3` reports a failure to prepare the *next* URI on the same bus as a
// failure of the one playing, so the engine ended a stream nobody had
// complained about. `Engine::setNextSource` already declines a next source that
// cannot be opened, which covers a playlist pointing at files that have moved
// or been deleted — but a `stat` cannot tell that a file will not decode, and
// this is what was left.
//
// The file here is bytes that are not audio under a name that says they are:
// readable, so the existence check passes it, and undecodable, so preparing it
// fails.
void runCorruptNextEntry(const QString &good)
{
    std::printf("\ncorrupt next entry (BUG-027)\n");

    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        check(false, "a scratch directory to put the broken file in");
        return;
    }
    // All three staged in the scratch directory under names that sort into the
    // order wanted. `addPaths` sorts, and letting the real paths decide put the
    // broken entry last rather than in the middle — the same trap that made two
    // of BUG-025's four reported failures test artefacts.
    const QString first = scratch.filePath(QStringLiteral("1-good.flac"));
    const QString broken = scratch.filePath(QStringLiteral("2-broken.flac"));
    const QString third = scratch.filePath(QStringLiteral("3-good.flac"));
    if (!QFile::copy(good, first) || !QFile::copy(good, third)) {
        check(false, "the good file could be staged twice");
        return;
    }
    {
        QFile file(broken);
        if (!file.open(QIODevice::WriteOnly)) {
            check(false, "the broken file could be written");
            return;
        }
        // Not silence and not a truncated header: a truncated FLAC still
        // declares its full duration and decodes part way, which made two of
        // BUG-025's four reported failures test artefacts. This is refused by
        // typefind outright.
        QByteArray rubbish(64 * 1024, '\0');
        for (int i = 0; i < rubbish.size(); ++i)
            rubbish[i] = char((i * 37 + 11) & 0xff);
        file.write(rubbish);
    }

    Player player;
    Engine &engine = *player.engine();
    PlaylistModel &playlist = *player.playlist();

    playlist.addPaths({ QUrl::fromLocalFile(first),
                        QUrl::fromLocalFile(broken),
                        QUrl::fromLocalFile(third) });
    check(playlist.rowCount() == 3, "three entries, the middle one broken",
          QStringLiteral("%1 rows").arg(playlist.rowCount()));
    if (playlist.rowCount() != 3)
        return;

    check(playlist.data(playlist.index(1, 0), PlaylistModel::UrlRole)
              .toString().contains(QStringLiteral("2-broken")),
          "and the broken one really is the middle entry",
          playlist.data(playlist.index(1, 0), PlaylistModel::UrlRole).toString());

    playlist.setCurrentRow(0);
    check(spin(engine, [&] { return engine.state() == Engine::Playing; }, 8000),
          "the good entry plays");
    check(spin(engine, [&] { return engine.duration() > 0; }, 5000),
          "and reports a duration", ms(engine.duration()));

    const qint64 duration = engine.duration();

    // Straight to the last few seconds. The handover is armed about a second
    // before the end whatever the file's length, so this exercises the same
    // path as playing the whole thing and does not spend half a minute doing it.
    engine.seek(duration - 4 * kSecond);
    check(spin(engine, [&] { return engine.position() > duration - 5 * kSecond; }, 5000),
          "seeking to the last few seconds, where the handover is armed",
          ms(engine.position()));

    // Watch it to the end, keeping the furthest position seen. The engine's
    // position resets when the stream ends, so the last value read before that
    // is what says how much of the file was played.
    qint64 furthest = 0;
    const QUrl started = engine.source();
    spin(engine, [&] {
        furthest = std::max(furthest, engine.position());
        return engine.source() != started || playlist.currentRow() != 0;
    }, 15000);

    check(duration > 0 && furthest >= duration - kSecond / 4,
          "it plays to the end rather than being cut short by the broken entry",
          QStringLiteral("%1 of %2").arg(ms(furthest), ms(duration)));

    // And the other half: with the handover refused there is no EOS from
    // `playbin3` at all, so a track that survived would simply sit there. The
    // playlist has to move on by itself.
    check(spin(engine, [&] { return playlist.currentRow() != 0; }, 15000),
          "and the playlist moves on rather than sitting on a finished track",
          QStringLiteral("row %1").arg(playlist.currentRow()));

    // Where it moved on *to* is the broken entry, which fails as the current
    // source and is stepped over by the machinery BUG-025 added. What matters
    // here is that it ends up playing again rather than stopping.
    // Read after the spin, not inside the same call. The order in which a
    // function's arguments are evaluated is unspecified, and this reported
    // "row 1" beside a passing check that requires row 2 — a detail string
    // describing the state before the wait it is meant to describe.
    const bool steppedOver = spin(engine, [&] {
        return engine.state() == Engine::Playing && playlist.currentRow() == 2;
    }, 15000);
    check(steppedOver, "stepping over the broken entry onto the third",
          QStringLiteral("row %1, state %2").arg(playlist.currentRow()).arg(engine.state()));
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    const QStringList files = app.arguments().mid(1);
    if (files.size() < 2) {
        std::fprintf(stderr, "usage: acceptance_transport <flac> <vbr-mp3> [reference-tone]\n");
        return 2;
    }

    runGainLaws();

    // Only the first two: a third argument, when given, is the reference tone
    // for the meter checks and is deliberately short, so the transport
    // expectations about duration do not apply to it.
    for (const QString &file : files.mid(0, 2))
        runFile(file);

    runPipelineFormat(files.first());

    // A steady tone at reference level if one is supplied, since a needle
    // settling on a known value says more than one twitching at a percussive
    // fixture. Falls back to the first file so the suite still runs without it.
    runMeters(files.size() > 2 ? files.at(2) : files.first());

    runPlayOrder(files.first(), files.at(1));
    runTransportLatency(files.first());
    runCorruptNextEntry(files.first());

    runGapless(files.first(), files.at(1));

    runStopStartCycles(files.first(), 20);

    return ferrolux::tests::summary();
}
