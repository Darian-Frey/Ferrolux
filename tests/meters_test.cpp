// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 4 meter tests. All of this is pure CPU state evolution, which is the
// whole reason D-005 puts ballistics here rather than in a shader: a needle's
// behaviour can be measured without rendering a single pixel.

#include <QCoreApplication>
#include <QList>

#include <cmath>
#include <cstdio>

#include "meters/MeterSource.h"

using ferrolux::meters::MeterSource;

namespace {

int failures = 0;

void check(bool ok, const char *what, const QString &detail = {})
{
    std::printf("  [%s] %s%s%s\n", ok ? "pass" : "FAIL", what,
                detail.isEmpty() ? "" : " — ", detail.isEmpty() ? "" : qPrintable(detail));
    std::fflush(stdout);
    if (!ok)
        ++failures;
}

constexpr int kBins = 512;
constexpr int kRate = 44100;

// A spectrum frame that is silent everywhere except one analysis bin.
QList<float> toneAt(int bin, float peakDb = -6.0f)
{
    QList<float> frame(kBins, float(MeterSource::kFloorDb));
    if (bin >= 0 && bin < kBins)
        frame[bin] = peakDb;
    return frame;
}

void testBandMapping()
{
    std::printf("\nlogarithmic band mapping (AV-011)\n");

    for (int bands : { MeterSource::kSpectrumBands, MeterSource::kMirroredBands }) {
        const auto ranges = MeterSource::bandRanges(bands, kBins, kRate);
        check(ranges.size() == bands,
              "every display band has a range",
              QStringLiteral("%1 of %2 bands").arg(ranges.size()).arg(bands));

        bool allValid = true;
        int interpolated = 0;
        for (const auto &range : ranges) {
            if (range.first < 0 || range.last < range.first || range.last >= kBins)
                allValid = false;
            if (range.interpolated)
                ++interpolated;
        }
        check(allValid,
              QStringLiteral("no band is empty or out of range at %1 bands").arg(bands).toUtf8().constData());
        check(interpolated > 0 && interpolated < bands,
              QStringLiteral("the lowest bands are marked interpolated at %1 bands").arg(bands).toUtf8().constData(),
              QStringLiteral("%1 of %2").arg(interpolated).arg(bands));
    }

    // The founding concern: bass must not collapse into a single bar. If the
    // bottom two octaves shared one band, they would share one range.
    const auto ranges = MeterSource::bandRanges(MeterSource::kSpectrumBands, kBins, kRate);
    const double binWidth = (double(kRate) / 2.0) / kBins;
    int below200 = 0;
    for (const auto &range : ranges) {
        if ((double(range.first) + 0.5) * binWidth < 200.0)
            ++below200;
    }
    check(below200 >= 8, "content below 200 Hz spreads across many bands, not one",
          QStringLiteral("%1 bands").arg(below200));
}

void testToneLandsInItsOwnBand()
{
    std::printf("\ntone placement (AV-011)\n");

    const auto ranges = MeterSource::bandRanges(MeterSource::kSpectrumBands, kBins, kRate);

    int tested = 0;
    int correct = 0;
    for (int band = 0; band < ranges.size(); ++band) {
        if (ranges.at(band).interpolated)
            continue; // shares a bin with its neighbours; see below

        MeterSource source;
        const QList<float> frame = toneAt(ranges.at(band).first);
        // Settle the smoothing rather than judging on one update.
        for (int i = 0; i < 40; ++i)
            source.consumeSpectrum(frame, kRate);

        // "A maximum" rather than "the unique maximum". The nearest-bin
        // fallback has to borrow a bin from somewhere, and the nearest bin
        // already belongs to a real band — at 24 bands, band 3 borrows bin 1
        // from band 4 and band 6 borrows bin 3 from band 7. A tone there lights
        // both equally. Demanding uniqueness would be demanding that
        // interpolation not happen.
        float highest = 0.0f;
        for (float value : source.magnitudes())
            highest = std::max(highest, value);

        ++tested;
        if (source.magnitudes().at(band) >= highest - 1e-6f)
            ++correct;
    }

    check(tested > 0, "there are non-interpolated bands to test",
          QStringLiteral("%1 bands").arg(tested));
    check(correct == tested,
          "a tone at a band's own bin peaks in that band",
          QStringLiteral("%1 of %2").arg(correct).arg(tested));

    // How much of the low end moves in lockstep, which is the visible cost of
    // the fallback and worth knowing rather than discovering on screen.
    const auto all = MeterSource::bandRanges(MeterSource::kSpectrumBands, kBins, kRate);
    int shared = 0;
    for (int i = 1; i < all.size(); ++i) {
        if (all.at(i).first <= all.at(i - 1).last)
            ++shared;
    }
    check(shared <= 6, "only the lowest bands share an analysis bin",
          QStringLiteral("%1 of %2 bands share with their neighbour")
              .arg(shared).arg(all.size()));

    // Interpolated bands cannot satisfy that — several share one analysis bin,
    // so a tone in it lights all of them. What must hold is that none is silent.
    MeterSource source;
    source.consumeSpectrum(toneAt(ranges.at(0).first), kRate);
    check(source.magnitudes().at(0) > 0.0f,
          "an interpolated band still responds rather than reading silent");
}

void testSmoothingAsymmetry()
{
    std::printf("\nspectrum smoothing\n");

    MeterSource rising;
    const QList<float> loud = toneAt(23, 0.0f);
    rising.consumeSpectrum(loud, kRate);
    const float afterOneAttack = rising.magnitudes().at(
        [] { const auto r = MeterSource::bandRanges(MeterSource::kSpectrumBands, kBins, kRate);
             for (int i = 0; i < r.size(); ++i) if (23 >= r.at(i).first && 23 <= r.at(i).last) return i;
             return 0; }());

    check(afterOneAttack > 0.5f, "a loud band rises most of the way in one update",
          QStringLiteral("%1 after one attack step").arg(afterOneAttack, 0, 'f', 3));

    // Now let it fall, and confirm the fall is slower than the rise.
    const QList<float> silent(kBins, float(MeterSource::kFloorDb));
    MeterSource falling;
    for (int i = 0; i < 40; ++i)
        falling.consumeSpectrum(loud, kRate);
    const float settled = falling.magnitudes().at(0) >= 0.0f ? 1.0f : 1.0f;
    Q_UNUSED(settled)
    float before = 0.0f;
    for (float value : falling.magnitudes())
        before = std::max(before, value);
    falling.consumeSpectrum(silent, kRate);
    float after = 0.0f;
    for (float value : falling.magnitudes())
        after = std::max(after, value);

    const double fallFraction = before > 0.0f ? double(before - after) / before : 0.0;
    check(fallFraction > 0.05 && fallFraction < 0.40,
          "and falls more slowly than it rose",
          QStringLiteral("%1% of the way in one release step").arg(fallFraction * 100.0, 0, 'f', 1));
}

void testPeakHold()
{
    std::printf("\npeak-hold caps\n");

    MeterSource source;
    const QList<float> loud = toneAt(23, 0.0f);
    for (int i = 0; i < 40; ++i)
        source.consumeSpectrum(loud, kRate);

    int band = 0;
    for (int i = 1; i < source.magnitudes().size(); ++i)
        if (source.magnitudes().at(i) > source.magnitudes().at(band))
            band = i;

    const float heldPeak = source.peaks().at(band);
    check(heldPeak > 0.9f, "the cap rises to the signal", QString::number(heldPeak, 'f', 3));

    // Silence, then hold for less than the TTL: the cap must not have moved.
    const QList<float> silent(kBins, float(MeterSource::kFloorDb));
    for (int i = 0; i < 10; ++i) {
        source.consumeSpectrum(silent, kRate);
        source.advance(16.0);
    }
    check(qFuzzyCompare(source.peaks().at(band) + 1.0f, heldPeak + 1.0f),
          "and holds while the TTL runs",
          QStringLiteral("%1 after 160 ms").arg(source.peaks().at(band), 0, 'f', 3));

    // Past the TTL it falls at the specified rate.
    for (int i = 0; i < 100; ++i) {
        source.consumeSpectrum(silent, kRate);
        source.advance(16.0);
    }
    check(source.peaks().at(band) < heldPeak,
          "then falls once the TTL expires",
          QStringLiteral("%1").arg(source.peaks().at(band), 0, 'f', 3));
}

// AV-012. The single most important behaviour in the meter design, and the one
// most easily destroyed by a smoothing change made for the spectrum.
void testVuBallistics()
{
    std::printf("\nVU ballistics (AV-012, BUG-010)\n");

    MeterSource source;
    // A steady tone at reference level: 0 VU, so the needle should settle at 1.
    const QList<double> reference { MeterSource::kVuReferenceDb, MeterSource::kVuReferenceDb };
    const QList<double> silence { -120.0, -120.0 };
    source.consumeLevel(reference, silence, silence);

    constexpr double kStepMs = 1.0; // fine steps, so the timing is measured not sampled
    double elapsed = 0.0;
    double timeTo99 = -1.0;
    double peak = 0.0;
    double timeOfPeak = 0.0;

    for (int step = 0; step < 2000; ++step) {
        source.advance(kStepMs);
        elapsed += kStepMs;
        const double deflection = source.vuDeflection(0);
        if (timeTo99 < 0.0 && deflection >= 0.99)
            timeTo99 = elapsed;
        if (deflection > peak) {
            peak = deflection;
            timeOfPeak = elapsed;
        }
    }

    check(timeTo99 > 0.0, "the needle reaches 99% of full deflection");
    check(timeTo99 > 285.0 && timeTo99 < 315.0,
          "at 300 ms within 5%, per IEC 60268-17",
          QStringLiteral("%1 ms").arg(timeTo99, 0, 'f', 1));

    const double overshootPercent = (peak - 1.0) * 100.0;
    check(overshootPercent >= 0.8 && overshootPercent <= 1.7,
          "overshooting by 1% to 1.5% — which a first-order system cannot do at all",
          QStringLiteral("%1% at %2 ms").arg(overshootPercent, 0, 'f', 2).arg(timeOfPeak, 0, 'f', 0));

    check(std::fabs(source.vuDeflection(0) - 1.0) < 0.01,
          "and settles at 0 VU for a reference-level signal",
          QString::number(source.vuDeflection(0), 'f', 4));

    // The degeneration AV-012 warns about: a meter that follows instantly is a
    // peak meter wearing the wrong face. After a single 16 ms update the needle
    // must still be nowhere near its target.
    MeterSource fresh;
    fresh.consumeLevel(reference, silence, silence);
    fresh.advance(16.0);
    check(fresh.vuDeflection(0) < 0.10,
          "one update moves the needle barely at all, so it is not a peak meter",
          QStringLiteral("%1 after 16 ms").arg(fresh.vuDeflection(0), 0, 'f', 4));

    // Reference level is the anchor: a signal 6 dB below reference must read
    // half the deflection, not half the decibels.
    MeterSource quiet;
    const QList<double> sixDown { MeterSource::kVuReferenceDb - 6.0,
                                  MeterSource::kVuReferenceDb - 6.0 };
    quiet.consumeLevel(sixDown, silence, silence);
    for (int i = 0; i < 2000; ++i)
        quiet.advance(1.0);
    check(std::fabs(quiet.vuDeflection(0) - 0.5012) < 0.02,
          "6 dB below reference settles at half deflection",
          QString::number(quiet.vuDeflection(0), 'f', 4));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testBandMapping();
    testToneLandsInItsOwnBand();
    testSmoothingAsymmetry();
    testPeakHold();
    testVuBallistics();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
