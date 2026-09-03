// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 4 meter tests. All of this is pure CPU state evolution, which is the
// whole reason D-005 puts ballistics here rather than in a shader: a needle's
// behaviour can be measured without rendering a single pixel.

#include <QCoreApplication>
#include <QList>
#include <QPair>

#include <cmath>
#include <cstdio>

#include "meters/MeterSource.h"
#include "meters/MeterTexture.h"

using ferrolux::meters::MeterSource;
using ferrolux::meters::MeterTexture;

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

    // Interpolated bands share bins with their neighbours, so a tone in one
    // lights several. What must hold is that none is silent.
    MeterSource source;
    source.consumeSpectrum(toneAt(ranges.at(0).first), kRate);
    check(source.magnitudes().at(0) > 0.0f,
          "an interpolated band still responds rather than reading silent");

    // And that they do not move as one welded group. Rounding a starved band to
    // its nearest bin gave several bands one identical value, which showed as
    // three bars at the bottom of the display locked rigidly together.
    QList<float> sloped(kBins, float(MeterSource::kFloorDb));
    for (int bin = 0; bin < 12; ++bin)
        sloped[bin] = float(MeterSource::kFloorDb + 6.0 * bin); // a ramp across the low bins

    MeterSource ramped;
    for (int i = 0; i < 40; ++i)
        ramped.consumeSpectrum(sloped, kRate);

    int interpolatedBands = 0;
    int distinctValues = 0;
    QList<float> seen;
    for (int band = 0; band < ranges.size(); ++band) {
        if (!ranges.at(band).interpolated)
            continue;
        ++interpolatedBands;
        const float value = ramped.magnitudes().at(band);
        bool duplicate = false;
        for (float other : seen)
            duplicate = duplicate || std::fabs(other - value) < 1e-6f;
        if (!duplicate)
            ++distinctValues;
        seen.append(value);
    }

    check(interpolatedBands > 1, "there is more than one interpolated band to compare",
          QString::number(interpolatedBands));
    check(distinctValues == interpolatedBands,
          "each interpolated band takes its own value across a sloped spectrum",
          QStringLiteral("%1 distinct of %2").arg(distinctValues).arg(interpolatedBands));
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
    const QList<double> reference { MeterSource::kDefaultReferenceDb, MeterSource::kDefaultReferenceDb };
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
    const QList<double> sixDown { MeterSource::kDefaultReferenceDb - 6.0,
                                  MeterSource::kDefaultReferenceDb - 6.0 };
    quiet.consumeLevel(sixDown, silence, silence);
    for (int i = 0; i < 2000; ++i)
        quiet.advance(1.0);
    check(std::fabs(quiet.vuDeflection(0) - 0.5012) < 0.02,
          "6 dB below reference settles at half deflection",
          QString::number(quiet.vuDeflection(0), 'f', 4));
}

// The texture packing. A shader reads these bytes and reconstructs the values,
// so encode and decode have to agree exactly — and the whole point of packing
// magnitude across two channels is precision the display would otherwise lose.
// The scales, checked against levels measured from ordinary music rather than
// against ideal ones. Mean RMS -13.7 dBFS with peaks near -10.6 is what a
// consumer master actually delivers, and a meter that pegs on it is not a
// meter. See BUG-014.
void testScalesAgainstRealMaterial()
{
    std::printf("\nscales against real material (BUG-014)\n");

    const QList<double> silence { -120.0, -120.0 };

    const auto settle = [&](double rmsDb, double peakDb) {
        auto *source = new MeterSource;
        source->consumeLevel({ rmsDb, rmsDb }, silence, { peakDb, peakDb });
        for (int i = 0; i < 2000; ++i)
            source->advance(1.0);
        const double vu = source->vuDeflection(0);
        const double peak = source->peakIndicator(0);
        delete source;
        return QPair<double, double>(vu, peak);
    };

    // A signal at the reference level is 0 VU by definition.
    const auto atReference = settle(MeterSource::kDefaultReferenceDb, -60.0);
    check(std::fabs(atReference.first - 1.0) < 0.01,
          "a signal at the reference level reads 0 VU",
          QString::number(atReference.first, 'f', 4));

    // Ordinary loud material: should sit just under 0 VU, with headroom left
    // for the peaks rather than pinned against the end stop.
    const auto typical = settle(-15.0, -10.6);
    check(typical.first > 0.35 && typical.first < 0.8,
          "typical music sits below 0 VU with room above",
          QStringLiteral("%1 for -15 dBFS RMS").arg(typical.first, 0, 'f', 3));

    const auto loudest = settle(-6.3, -1.0);
    check(loudest.first > 1.0 && loudest.first <= 1.4,
          "the loudest real material lands at the top of the travel without pegging",
          QStringLiteral("%1 for -6.3 dBFS RMS").arg(loudest.first, 0, 'f', 3));

    // The old -18 dBFS broadcast reference is what pegged it.
    auto *broadcast = new MeterSource;
    broadcast->setReferenceLevel(-18.0);
    broadcast->consumeLevel({ -13.7, -13.7 }, silence, silence);
    for (int i = 0; i < 2000; ++i)
        broadcast->advance(1.0);
    check(broadcast->vuDeflection(0) > 1.5,
          "and the -18 dBFS broadcast reference would have pegged it",
          QStringLiteral("%1 — past the end of the scale").arg(broadcast->vuDeflection(0), 0, 'f', 3));
    delete broadcast;

    // Peaks are a decibel scale ending at full scale, not the VU's ratio.
    check(typical.second > 0.7 && typical.second < 0.9,
          "a -10.6 dBFS peak reads high on the peak indicator, which drives the lamp",
          QString::number(typical.second, 'f', 3));
    check(std::fabs(typical.first - 0.5) < 0.15,
          "and the ladder, driven by the ballistic level, has room to move",
          QStringLiteral("%1 of 1.25 full-scale").arg(typical.first, 0, 'f', 3));
    check(loudest.second > 0.95,
          "a -1 dBFS peak reads nearly full, as it should",
          QString::number(loudest.second, 'f', 3));
    check(settle(-60.0, -60.0).second < 0.02,
          "and the floor of the peak scale reads empty");
}

// Stop and pause are different events. A paused deck holds; a stopped one falls
// back to rest under the same ballistic that raised it.
void testRestAndHold()
{
    std::printf("\nrest and hold\n");

    MeterSource source;
    const QList<double> loud { -6.0, -6.0 };
    const QList<double> silence { -120.0, -120.0 };
    const QList<float> tone = toneAt(23, 0.0f);

    for (int i = 0; i < 60; ++i) {
        source.consumeSpectrum(tone, kRate);
        source.consumeLevel(loud, silence, loud);
        source.advance(16.0);
    }

    float loudestBefore = 0.0f;
    for (float value : source.magnitudes())
        loudestBefore = std::max(loudestBefore, value);
    const double needleBefore = source.vuDeflection(0);
    check(loudestBefore > 0.5f && needleBefore > 0.5,
          "the display is showing signal before the transport stops",
          QStringLiteral("band %1, needle %2")
              .arg(loudestBefore, 0, 'f', 2).arg(needleBefore, 0, 'f', 2));

    // Pause: messages simply stop arriving. Nothing should move.
    for (int i = 0; i < 30; ++i)
        source.advance(16.0);
    float loudestPaused = 0.0f;
    for (float value : source.magnitudes())
        loudestPaused = std::max(loudestPaused, value);
    check(qFuzzyCompare(loudestPaused + 1.0f, loudestBefore + 1.0f),
          "pausing holds the bars where they were",
          QStringLiteral("%1 after 480 ms").arg(loudestPaused, 0, 'f', 3));
    check(std::fabs(source.vuDeflection(0) - needleBefore) < 0.02,
          "and holds the needle too",
          QString::number(source.vuDeflection(0), 'f', 3));

    // Stop: everything falls back, and gradually rather than at once.
    source.setReleasing(true);
    source.advance(16.0);
    float loudestFirstStep = 0.0f;
    for (float value : source.magnitudes())
        loudestFirstStep = std::max(loudestFirstStep, value);
    check(loudestFirstStep < loudestBefore && loudestFirstStep > loudestBefore * 0.5f,
          "stopping starts the bars falling without dropping them",
          QStringLiteral("%1 from %2 after one step")
              .arg(loudestFirstStep, 0, 'f', 3).arg(loudestBefore, 0, 'f', 3));
    check(source.vuDeflection(0) > needleBefore * 0.8,
          "and the needle has barely begun to move, being a physical system",
          QString::number(source.vuDeflection(0), 'f', 3));

    for (int i = 0; i < 200; ++i)
        source.advance(16.0);
    float loudestRested = 0.0f;
    for (float value : source.magnitudes())
        loudestRested = std::max(loudestRested, value);
    check(loudestRested < 0.01f, "and they reach rest",
          QStringLiteral("band %1").arg(loudestRested, 0, 'e', 1));
    check(source.vuDeflection(0) < 0.01,
          "as does the needle", QString::number(source.vuDeflection(0), 'f', 4));

    // Playing again must not leave it stuck in release.
    source.setReleasing(false);
    for (int i = 0; i < 60; ++i) {
        source.consumeSpectrum(tone, kRate);
        source.consumeLevel(loud, silence, loud);
        source.advance(16.0);
    }
    float loudestAgain = 0.0f;
    for (float value : source.magnitudes())
        loudestAgain = std::max(loudestAgain, value);
    check(loudestAgain > 0.5f, "and starting again brings it back",
          QStringLiteral("%1").arg(loudestAgain, 0, 'f', 2));
}

void testTexturePacking()
{
    std::printf("\ntexture packing (D-004)\n");

    uchar texel[4] = { 0, 0, 0, 0 };

    MeterTexture::encodeTexel(0.0f, 0.0f, texel);
    check(MeterTexture::decodeMagnitude(texel[0], texel[1]) == 0.0f
              && MeterTexture::decodePeak(texel[2]) == 0.0f,
          "silence encodes to zero");

    MeterTexture::encodeTexel(1.0f, 1.0f, texel);
    check(MeterTexture::decodeMagnitude(texel[0], texel[1]) == 1.0f
              && MeterTexture::decodePeak(texel[2]) == 1.0f,
          "full scale encodes to one");

    check(texel[3] == 255, "alpha is held opaque so nothing premultiplies the data");

    // Round-trip error across the range. Sixteen bits gives a resolution of
    // 1/65535; eight would give 1/255, which on a 400-pixel bar at 4K is a
    // 1.6-pixel step visible as stair-stepping on a slow decay.
    double worstMagnitude = 0.0;
    double worstPeak = 0.0;
    for (int i = 0; i <= 2000; ++i) {
        const float value = float(i) / 2000.0f;
        MeterTexture::encodeTexel(value, value, texel);
        worstMagnitude = std::max(worstMagnitude,
                                  std::fabs(double(MeterTexture::decodeMagnitude(texel[0], texel[1]) - value)));
        worstPeak = std::max(worstPeak,
                             std::fabs(double(MeterTexture::decodePeak(texel[2]) - value)));
    }
    check(worstMagnitude < 1.0 / 65535.0,
          "magnitude round-trips at 16-bit resolution",
          QStringLiteral("worst error %1").arg(worstMagnitude, 0, 'e', 2));
    check(worstMagnitude * 255.0 < 1.0 / 200.0,
          "which is far finer than the 8 bits a single channel would give",
          QStringLiteral("%1x better than 1/255").arg((1.0 / 255.0) / worstMagnitude, 0, 'f', 0));
    check(worstPeak < 1.0 / 255.0,
          "the peak cap round-trips at 8-bit resolution, which positions a line",
          QStringLiteral("worst error %1").arg(worstPeak, 0, 'e', 2));

    // Values outside the normalised range must clamp rather than wrap: a VU
    // deflection above 0 VU legitimately exceeds 1.0.
    MeterTexture::encodeTexel(2.5f, -1.0f, texel);
    check(MeterTexture::decodeMagnitude(texel[0], texel[1]) == 1.0f
              && MeterTexture::decodePeak(texel[2]) == 0.0f,
          "out-of-range values clamp rather than wrapping around");
}

// The bound the flame shader dismisses empty pixels with. It has to be a true
// upper bound on every band, or the shader will discard a pixel a silhouette
// actually reached and cut the top off the display — a defect that would appear
// only on the loudest frames, which is where nobody looks for one.
void testCeiling()
{
    std::printf("\nspectrum ceiling (BUG-016)\n");

    MeterSource meters;
    meters.setBandCount(MeterSource::kSpectrumBands);
    check(meters.ceiling() == 0.0, "silence has a ceiling of zero");

    QList<float> spectrum;
    for (int bin = 0; bin < 512; ++bin)
        spectrum.append(bin == 40 ? -3.0f : -70.0f);

    // Enough frames for the attack smoothing to settle on the loud band.
    for (int frame = 0; frame < 120; ++frame) {
        meters.consumeSpectrum(spectrum, 44100);
        meters.advance(16.0);
    }

    float tallest = 0.0f;
    for (float magnitude : meters.magnitudes())
        tallest = std::max(tallest, magnitude);

    check(std::fabs(meters.ceiling() - double(tallest)) < 1e-9,
          "the ceiling is the tallest band, exactly",
          QStringLiteral("ceiling %1, tallest band %2")
              .arg(meters.ceiling(), 0, 'f', 6).arg(double(tallest), 0, 'f', 6));

    bool bounded = true;
    for (float magnitude : meters.magnitudes())
        bounded = bounded && double(magnitude) <= meters.ceiling() + 1e-9;
    check(bounded, "and no band exceeds it, which is what makes it safe to cull by");

    check(meters.ceiling() > 0.0 && meters.ceiling() <= 1.0,
          "it stays within the normalised range the shader compares heights against",
          QStringLiteral("%1").arg(meters.ceiling(), 0, 'f', 4));
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
    testScalesAgainstRealMaterial();
    testRestAndHold();
    testTexturePacking();
    testCeiling();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
