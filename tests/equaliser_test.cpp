// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Phase 3 equaliser tests. Two halves: pure gain arithmetic that needs no
// pipeline, and two offline captures that answer questions only real audio can
// — whether bypass is genuinely bit-identical (F-020's acceptance criterion)
// and whether the headroom rule actually holds under the worst case AV-003
// describes.

#include <QCoreApplication>
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

void testHeadroom()
{
    std::printf("\nheadroom arithmetic (AV-003)\n");

    const QList<double> flat(Equaliser::kBandCount, 0.0);
    const QList<double> maxed(Equaliser::kBandCount, 12.0);
    const QList<double> cut(Equaliser::kBandCount, -12.0);

    check(qFuzzyIsNull(Equaliser::headroomAttenuation(0.0, flat)),
          "a flat curve needs no attenuation");
    const double worst = Equaliser::headroomAttenuation(12.0, maxed);
    check(worst > 33.0 && worst < 37.0,
          "the worst case asks for the cascade peak plus preamp, not max_band plus preamp",
          QStringLiteral("%1 dB — the old max_band rule said 24").arg(worst, 0, 'f', 2));

    // The peak is sampled on a log grid rather than solved for, so it lands
    // just under the true maximum. A hundredth of a decibel is far inside any
    // margin that matters.
    const double single = Equaliser::cascadePeakGain({ 0, 0, 0, 12, 0, 0, 0, 0, 0, 0 });
    check(std::fabs(single - 12.0) < 0.01,
          "a single boosted band peaks at its own gain",
          QStringLiteral("%1 dB").arg(single, 0, 'f', 4));
    check(Equaliser::cascadePeakGain({ 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }) > 20.0,
          "ten overlapping bands peak well above any one of them",
          QStringLiteral("%1 dB")
              .arg(Equaliser::cascadePeakGain({ 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }), 0, 'f', 2));
    check(qFuzzyIsNull(Equaliser::cascadePeakGain(flat)),
          "a flat curve has unity cascade gain");
    check(qFuzzyIsNull(Equaliser::headroomAttenuation(-12.0, cut)),
          "an all-cut curve needs no attenuation");
    check(qFuzzyIsNull(Equaliser::headroomAttenuation(-6.0, { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 })),
          "a preamp cut exactly offsetting a boost needs none");
    check(Equaliser::headroomAttenuation(0.0, { 0, 0, 0, 3, 0, 0, 0, 0, 0, 0 }) > 0.0,
          "a single boosted band still asks for attenuation");
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
    check(std::isfinite(peak), "no sample is infinite or NaN — the filters stayed stable",
          QStringLiteral("peak %1").arg(peak, 0, 'f', 4));
    check(peak <= 1.0,
          "ten bands at +12 dB with a +12 dB preamp does not exceed full scale",
          QStringLiteral("peak %1 (%2 dBFS)")
              .arg(peak, 0, 'f', 4)
              .arg(peak > 0 ? 20.0 * std::log10(peak) : -99.0, 0, 'f', 2));
}

} // namespace

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);

    testConfiguration();
    testHeadroom();
    testEqf();
    testBypassIsBitIdentical();
    testWorstCaseGain();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
