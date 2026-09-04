// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 3 equaliser tests. Two halves: pure gain arithmetic that needs no
// pipeline, and two offline captures that answer questions only real audio can
// — whether bypass is genuinely bit-identical (F-020's acceptance criterion)
// and whether the headroom rule actually holds under the worst case AV-003
// describes.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QByteArray>
#include <QList>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <cmath>
#include <cstdio>

#include "core/Equaliser.h"

using ferrolux::core::Equaliser;

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

// Runs a fixed, deterministic signal through an optional equaliser and returns
// the raw output bytes. Everything is built from the same element factories the
// engine uses, so this tests the real configuration rather than a stand-in.
QByteArray capture(Equaliser *equaliser, const char *format, int numBuffers = 60)
{
    GstElement *pipeline = gst_pipeline_new("capture");
    GstElement *src = gst_element_factory_make("audiotestsrc", nullptr);
    GstElement *convertIn = gst_element_factory_make("audioconvert", nullptr);
    GstElement *capsIn = gst_element_factory_make("capsfilter", nullptr);
    GstElement *convertOut = gst_element_factory_make("audioconvert", nullptr);
    GstElement *capsOut = gst_element_factory_make("capsfilter", nullptr);
    GstElement *sink = gst_element_factory_make("appsink", nullptr);

    if (!pipeline || !src || !convertIn || !capsIn || !convertOut || !capsOut || !sink)
        return {};

    // Sawtooth: broadband, so every band has something to act on, and entirely
    // deterministic, so two runs are comparable.
    g_object_set(src, "num-buffers", numBuffers, "wave", 2, "freq", 220.0,
                 "volume", 1.0, nullptr);

    GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                        "format", G_TYPE_STRING, format,
                                        "rate", G_TYPE_INT, 44100,
                                        "channels", G_TYPE_INT, 2,
                                        "layout", G_TYPE_STRING, "interleaved",
                                        nullptr);
    g_object_set(capsIn, "caps", caps, nullptr);
    g_object_set(capsOut, "caps", caps, nullptr);
    gst_caps_unref(caps);
    g_object_set(sink, "sync", FALSE, nullptr);

    if (equaliser && equaliser->createElements()) {
        gst_bin_add_many(GST_BIN(pipeline), src, convertIn, capsIn,
                         equaliser->preampElement(), equaliser->filterElement(),
                         convertOut, capsOut, sink, nullptr);
        gst_element_link_many(src, convertIn, capsIn, equaliser->preampElement(),
                              equaliser->filterElement(), convertOut, capsOut,
                              sink, nullptr);
    } else {
        gst_bin_add_many(GST_BIN(pipeline), src, convertIn, capsIn,
                         convertOut, capsOut, sink, nullptr);
        gst_element_link_many(src, convertIn, capsIn, convertOut, capsOut, sink, nullptr);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    QByteArray collected;
    for (;;) {
        GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink),
                                                         5 * GST_SECOND);
        if (!sample)
            break;
        if (GstBuffer *buffer = gst_sample_get_buffer(sample)) {
            GstMapInfo info;
            if (gst_buffer_map(buffer, &info, GST_MAP_READ)) {
                collected.append(reinterpret_cast<const char *>(info.data), qsizetype(info.size));
                gst_buffer_unmap(buffer, &info);
            }
        }
        gst_sample_unref(sample);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return collected;
}

double peakOf(const QByteArray &floats)
{
    double peak = 0.0;
    const auto *values = reinterpret_cast<const float *>(floats.constData());
    const qsizetype count = floats.size() / qsizetype(sizeof(float));
    for (qsizetype i = 0; i < count; ++i)
        peak = std::max(peak, double(std::fabs(values[i])));
    return peak;
}

void testConfiguration()
{
    std::printf("\nband configuration (D-007)\n");

    const auto &centres = Equaliser::centreFrequencies();
    const QList<double> expected = { 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };
    bool match = true;
    for (int i = 0; i < Equaliser::kBandCount; ++i)
        match = match && qFuzzyCompare(centres[size_t(i)], expected.at(i));
    check(match, "centre frequencies are Winamp's, per SPEC.md §Equaliser");

    const auto &widths = Equaliser::bandwidths();
    bool positive = true;
    bool contiguous = true;
    for (int i = 0; i < Equaliser::kBandCount; ++i) {
        positive = positive && widths[size_t(i)] > 0.0;
        if (i > 0 && widths[size_t(i)] <= 0.0)
            contiguous = false;
    }
    check(positive && contiguous, "every band has a positive bandwidth");

    Equaliser eq;
    eq.setBand(0, 99.0);
    check(qFuzzyCompare(eq.band(0), 12.0), "band gain clamps to +12 dB",
          QString::number(eq.band(0)));
    eq.setBand(0, -99.0);
    check(qFuzzyCompare(eq.band(0), -12.0), "band gain clamps to -12 dB");
    eq.setPreamp(50.0);
    check(qFuzzyCompare(eq.preamp(), 12.0), "preamp clamps to +12 dB");

    check(Equaliser::presetNames().contains(QStringLiteral("flat")),
          "the preset bank contains flat");
    eq.applyPreset(QStringLiteral("rock"));
    check(eq.preset() == QStringLiteral("rock"), "a preset can be recalled by name");
    eq.setBand(3, 5.0);
    check(eq.preset() == QStringLiteral("custom"),
          "editing a band marks the curve custom rather than lying about the preset");
}

void testExcessGain()
{
    std::printf("\nexcess gain reporting (AV-003)\n");

    const QList<double> flat(Equaliser::kBandCount, 0.0);
    const QList<double> maxed(Equaliser::kBandCount, 12.0);
    const QList<double> cut(Equaliser::kBandCount, -12.0);

    check(qFuzzyIsNull(Equaliser::excessGain(0.0, flat)),
          "a flat curve exceeds unity by nothing");
    const double worst = Equaliser::excessGain(12.0, maxed);
    check(worst > 33.0 && worst < 37.0,
          "the worst case is the cascade peak plus preamp, not max_band plus preamp",
          QStringLiteral("%1 dB").arg(worst, 0, 'f', 2));
    check(qFuzzyIsNull(Equaliser::excessGain(-12.0, cut)),
          "an all-cut curve exceeds unity by nothing");
    check(qFuzzyIsNull(Equaliser::excessGain(-6.0, { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 })),
          "a preamp cut exactly offsetting a boost exceeds unity by nothing");

    // The peak is sampled on a log grid rather than solved for, so it lands
    // just under the true maximum. A hundredth of a decibel is far inside any
    // margin that matters.
    const double single = Equaliser::cascadePeakGain({ 0, 0, 0, 12, 0, 0, 0, 0, 0, 0 });
    check(std::fabs(single - 12.0) < 0.01,
          "a single boosted band peaks at its own gain",
          QStringLiteral("%1 dB").arg(single, 0, 'f', 4));
    check(Equaliser::cascadePeakGain(maxed) > 20.0,
          "ten overlapping bands peak well above any one of them",
          QStringLiteral("%1 dB").arg(Equaliser::cascadePeakGain(maxed), 0, 'f', 2));

    // BUG-008: the figure is reported, never subtracted. A boost must boost.
    Equaliser eq;
    eq.setEnabled(true);
    eq.setPreamp(6.0);
    check(eq.excessGain() > 5.9 && eq.excessGain() < 6.1,
          "a raised preamp is reported as excess rather than cancelled",
          QStringLiteral("%1 dB").arg(eq.excessGain(), 0, 'f', 2));

    eq.setEnabled(false);
    check(qFuzzyIsNull(eq.excessGain()), "a disabled equaliser reports no excess");

    // The response evaluation runs on every gain change, so a slider drag runs
    // it at frame rate. It has to be cheap enough that dragging is free.
    QElapsedTimer timer;
    timer.start();
    volatile double sink = 0.0;
    constexpr int kRuns = 1000;
    for (int i = 0; i < kRuns; ++i)
        sink += Equaliser::cascadePeakGain(maxed);
    const double microseconds = double(timer.nsecsElapsed()) / 1000.0 / kRuns;
    // Generous, because a Debug build runs this about six times slower than a
    // Release one. The bound catches an order-of-magnitude regression, not the
    // constant factor.
    check(microseconds < 2000.0,
          "the worst-case response evaluation is cheap enough to run per gain change",
          QStringLiteral("%1 us per call").arg(microseconds, 0, 'f', 1));
}

void testEqf()
{
    std::printf("\nWinamp .eqf import (F-022)\n");

    // Eleven bytes: ten bands then the preamp. 31 is 0 dB and the scale is
    // inverted, so 0 is maximum boost and 63 maximum cut.
    QByteArray payload(11, char(31));
    QList<double> bands;
    double preamp = 99.0;
    check(Equaliser::decodeEqf(payload, &bands, &preamp),
          "a bare eleven-byte payload decodes");
    check(bands.size() == Equaliser::kBandCount, "ten bands are produced");
    bool allZero = true;
    for (double gain : bands)
        allZero = allZero && std::fabs(gain) < 1e-9;
    check(allZero && std::fabs(preamp) < 1e-9, "the value 31 maps to 0 dB");

    payload[0] = char(0);
    payload[1] = char(63);
    Equaliser::decodeEqf(payload, &bands, &preamp);
    check(bands.at(0) > 11.9 && bands.at(0) <= 12.0,
          "the value 0 is maximum boost, not maximum cut",
          QStringLiteral("%1 dB").arg(bands.at(0), 0, 'f', 2));
    check(bands.at(1) < -12.0 && bands.at(1) > -12.5,
          "the value 63 is maximum cut — the scale is inverted",
          QStringLiteral("%1 dB").arg(bands.at(1), 0, 'f', 2));

    QByteArray full(31 + 257 + 11, char(0));
    full.replace(31, 8, QByteArray("Deep Bass"));
    for (int i = 0; i < 11; ++i)
        full[31 + 257 + i] = char(31);
    QString name;
    check(Equaliser::decodeEqf(full, &bands, &preamp, &name),
          "a full library file with header and name decodes");
    check(name.startsWith(QStringLiteral("Deep Bas")), "the preset name is recovered", name);

    check(!Equaliser::decodeEqf(QByteArray("short"), &bands, &preamp),
          "a truncated file is rejected");
    QByteArray bad(11, char(31));
    bad[4] = char(100);
    check(!Equaliser::decodeEqf(bad, &bands, &preamp),
          "a byte outside 0..63 is rejected");
}

void testBypassIsBitIdentical()
{
    std::printf("\nbypass transparency (F-020 acceptance)\n");

    const QByteArray reference = capture(nullptr, "S16LE");
    check(!reference.isEmpty(), "reference capture produced audio",
          QStringLiteral("%1 bytes").arg(reference.size()));

    Equaliser bypassed;                       // default state is disabled
    const QByteArray throughEq = capture(&bypassed, "S16LE");
    check(throughEq.size() == reference.size(), "bypassed capture is the same length",
          QStringLiteral("%1 vs %2").arg(throughEq.size()).arg(reference.size()));
    check(throughEq == reference,
          "bypass output is bit-identical to the unprocessed signal");

    // Control: if the element were not actually in circuit, the test above
    // would pass for the wrong reason.
    Equaliser active;
    active.setEnabled(true);
    active.setBands({ 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 });
    const QByteArray boosted = capture(&active, "S16LE");
    check(boosted != reference,
          "an active equaliser does change the signal, so bypass is a real result");
}

void testWorstCaseGain()
{
    std::printf("\nworst-case gain (AV-003)\n");

    const QByteArray clean = capture(nullptr, "F32LE");
    const double sourcePeak = peakOf(clean);
    check(sourcePeak > 0.9, "the source is near full scale",
          QStringLiteral("%1").arg(sourcePeak, 0, 'f', 4));

    Equaliser extreme;
    extreme.setEnabled(true);
    extreme.setPreamp(12.0);
    extreme.setBands({ 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 });
    const QByteArray loud = capture(&extreme, "F32LE");
    const double peak = peakOf(loud);

    check(!loud.isEmpty(), "the extreme setting still produces audio");

    // What AV-003 is actually about. Its concern was that clipping inside an
    // IIR cascade can drive the filters unstable rather than merely distorting.
    // In F32LE, levels above unity are ordinary; instability would show as a
    // non-finite sample or a runaway magnitude.
    check(std::isfinite(peak), "no sample is infinite or NaN — the filters stayed stable",
          QStringLiteral("peak %1").arg(peak, 0, 'f', 4));
    check(peak < 1000.0, "the cascade does not run away",
          QStringLiteral("peak %1 (%2 dBFS)")
              .arg(peak, 0, 'f', 4)
              .arg(peak > 0 ? 20.0 * std::log10(peak) : -99.0, 0, 'f', 2));

    // BUG-008: the output is expected to exceed unity now, because nothing
    // attenuates it. The reported figure must bound what actually happens —
    // it is modelled at the highest supported rate and so should never
    // under-state the real peak at 44.1 kHz.
    const double measuredDb = peak > 0 ? 20.0 * std::log10(peak / sourcePeak) : -99.0;
    const double reported = extreme.excessGain();
    check(peak > 1.0, "an extreme boost does exceed full scale, as an equaliser should",
          QStringLiteral("%1 dBFS").arg(20.0 * std::log10(peak), 0, 'f', 2));
    check(measuredDb <= reported + 0.5,
          "the reported excess bounds the measured gain rather than under-stating it",
          QStringLiteral("measured %1 dB, reported %2 dB")
              .arg(measuredDb, 0, 'f', 2).arg(reported, 0, 'f', 2));
}

// SPEC.md §Equaliser specifies a 30 ms linear interpolation on any gain change,
// so that dragging a slider does not step the filter coefficients and click.
// The audible clause is not measured here; what is measured is that the applied
// curve genuinely travels rather than jumping, and lands exactly on target.
void testGainRamp()
{
    std::printf("\ngain ramp (F-020)\n");

    Equaliser eq;
    if (!eq.createElements()) {
        check(false, "equaliser elements available");
        return;
    }
    // The first write after the elements exist is the initial state and is
    // applied whole; everything after it ramps.
    check(!eq.isRamping(), "priming the elements does not start a ramp");

    eq.setEnabled(true);
    check(!eq.isRamping(), "enabling a flat curve changes nothing, so no ramp starts");

    eq.setBand(4, 12.0);
    check(eq.isRamping(), "a gain change starts a ramp");
    check(std::fabs(eq.appliedBands().value(4)) < 1.0,
          "the applied gain has not jumped to the target",
          QStringLiteral("%1 dB").arg(eq.appliedBands().value(4), 0, 'f', 2));
    check(qFuzzyCompare(eq.band(4), 12.0),
          "but the reported setting is the target, so the UI does not lag");

    QElapsedTimer clock;
    clock.start();
    double midpoint = -99.0;
    while (clock.elapsed() < 200) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        if (midpoint < -90.0 && clock.elapsed() >= 15)
            midpoint = eq.appliedBands().value(4);
        if (!eq.isRamping())
            break;
    }

    check(midpoint > 2.0 && midpoint < 10.0,
          "halfway through, the applied gain is partway there",
          QStringLiteral("%1 dB at ~15 ms of 30").arg(midpoint, 0, 'f', 2));
    check(!eq.isRamping(), "the ramp finishes",
          QStringLiteral("%1 ms").arg(clock.elapsed()));
    check(qFuzzyCompare(eq.appliedBands().value(4), 12.0),
          "and lands exactly on the target, not near it",
          QStringLiteral("%1 dB").arg(eq.appliedBands().value(4), 0, 'f', 6));

    // Bypassing is a gain change too, and would click just as loudly.
    eq.setEnabled(false);
    check(eq.isRamping(), "toggling bypass ramps rather than jumping");
    clock.restart();
    while (eq.isRamping() && clock.elapsed() < 200)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    check(qFuzzyIsNull(eq.appliedBands().value(4)),
          "and settles at unity when disabled");
}

// BUG-019: the preset name is saved on exit and has to survive a restart
// without ever being trusted.
//
// SPEC.md §Settings makes the band values authoritative on restore and the name
// descriptive, because "a preset may have been edited, or its definition may
// have changed since it was chosen". Restoring the name blindly is simpler and
// is wrong in exactly the case the specification bothered to describe, so what
// is checked here is that the name is adopted when the curve still *is* that
// preset and refused when it is not.
//
// The defect this replaces was invisible for two phases: the harness showed the
// wrong name in a combo box and nobody looked. It only surfaced once the panel
// had a lit field to show it in — which is an argument for checking it here
// rather than by eye.
void testPresetNameSurvivesRestart()
{
    std::printf("\npreset name across a restart (BUG-019)\n");

    // What a restart actually does: the bands and the preamp come back from
    // settings, and nothing has set a name.
    Equaliser eq;
    eq.setBands(Equaliser::presetBands(QStringLiteral("rock")));
    check(eq.preset() == QStringLiteral("custom"),
          "a curve set wholesale has no name until something gives it one",
          eq.preset());

    eq.adoptPreset(QStringLiteral("rock"));
    check(eq.preset() == QStringLiteral("rock"),
          "the remembered name is adopted when the curve is still that preset",
          eq.preset());

    // The curve is the authority. A band moved away from the preset means the
    // name no longer describes what is loaded.
    Equaliser edited;
    QList<double> altered = Equaliser::presetBands(QStringLiteral("rock"));
    altered[3] += 2.0;
    edited.setBands(altered);
    edited.adoptPreset(QStringLiteral("rock"));
    check(edited.preset() != QStringLiteral("rock"),
          "a curve that has drifted from the preset does not take its name back",
          edited.preset());

    // A name that no longer resolves: a user preset deleted, or a built-in
    // renamed between versions.
    Equaliser orphan;
    orphan.setBands(Equaliser::presetBands(QStringLiteral("rock")));
    orphan.adoptPreset(QStringLiteral("a preset that was removed"));
    check(orphan.preset() != QStringLiteral("a preset that was removed"),
          "a name that no longer resolves is not adopted");

    // A user preset carries a preamp where a built-in does not, so the preamp
    // is part of the comparison for one and not the other. Holding a built-in
    // to a preamp it never specified would make every preset read as edited the
    // moment the preamp moved.
    Equaliser withPreamp;
    withPreamp.setBands(Equaliser::presetBands(QStringLiteral("rock")));
    withPreamp.setPreamp(-6.0);
    withPreamp.adoptPreset(QStringLiteral("rock"));
    check(withPreamp.preset() == QStringLiteral("rock"),
          "a built-in is judged on its bands alone, because that is all it defines",
          withPreamp.preset());

    Equaliser owner;
    owner.setBands({ 6, 5, 4, 3, 2, 1, 0, -1, -2, -3 });
    owner.setPreamp(-4.0);
    owner.saveUserPreset(QStringLiteral("Restart Curve"));

    Equaliser sameCurve;
    sameCurve.setBands({ 6, 5, 4, 3, 2, 1, 0, -1, -2, -3 });
    sameCurve.setPreamp(-4.0);
    sameCurve.adoptPreset(QStringLiteral("Restart Curve"));
    check(sameCurve.preset() == QStringLiteral("Restart Curve"),
          "a user preset is adopted when its bands and its preamp both match",
          sameCurve.preset());

    Equaliser preampMoved;
    preampMoved.setBands({ 6, 5, 4, 3, 2, 1, 0, -1, -2, -3 });
    preampMoved.setPreamp(0.0);
    preampMoved.adoptPreset(QStringLiteral("Restart Curve"));
    check(preampMoved.preset() != QStringLiteral("Restart Curve"),
          "and refused when the preamp has moved, because a user preset defines one",
          preampMoved.preset());

    owner.removeUserPreset(QStringLiteral("Restart Curve"));
}

// F-022's user presets. Settings are redirected to a test location by main(),
// so nothing here can reach a real configuration file.
void testUserPresets()
{
    std::printf("\nuser presets (F-022)\n");

    Equaliser eq;
    eq.setBands({ 6, 5, 4, 3, 2, 1, 0, -1, -2, -3 });
    eq.setPreamp(-4.0);

    const QList<double> saved = eq.curve();
    check(saved.size() == Equaliser::kBandCount + 1,
          "a curve is eleven values — ten bands then the preamp",
          QString::number(saved.size()));

    check(eq.saveUserPreset(QStringLiteral("Test Curve")), "a user preset saves");
    check(eq.userPresetNames().contains(QStringLiteral("Test Curve")),
          "and appears in the user list");
    check(eq.availablePresets().contains(QStringLiteral("Test Curve")),
          "and in the combined chooser list");
    check(eq.preset() == QStringLiteral("Test Curve"),
          "saving names the current curve after the preset");

    // Move away, then come back.
    eq.applyPreset(QStringLiteral("flat"));
    check(qFuzzyIsNull(eq.band(0)), "moving to a built-in changes the curve");

    eq.applyPreset(QStringLiteral("Test Curve"));
    bool restored = qFuzzyCompare(eq.preamp() + 100.0, -4.0 + 100.0);
    for (int i = 0; i < Equaliser::kBandCount; ++i)
        restored = restored && qFuzzyCompare(eq.band(i) + 100.0, saved.at(i) + 100.0);
    check(restored, "recalling a user preset restores bands and preamp exactly");

    check(!eq.saveUserPreset(QString()), "an empty name is rejected");
    check(!eq.saveUserPreset(QStringLiteral("a/b")),
          "a name containing a slash is rejected — it would open a settings group");

    check(eq.removeUserPreset(QStringLiteral("Test Curve")), "a user preset is removable");
    check(!eq.userPresetNames().contains(QStringLiteral("Test Curve")),
          "and is gone afterwards");
    check(!eq.removeUserPreset(QStringLiteral("Test Curve")),
          "removing it twice reports failure rather than pretending");
}

} // namespace

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    // Redirect QSettings before anything can touch a real configuration file.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("ferrolux"));
    QCoreApplication::setApplicationName(QStringLiteral("ferrolux-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QCoreApplication app(argc, argv);

    testConfiguration();
    testExcessGain();
    testEqf();
    testBypassIsBitIdentical();
    testWorstCaseGain();
    testGainRamp();
    testUserPresets();
    testPresetNameSurvivesRestart();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
