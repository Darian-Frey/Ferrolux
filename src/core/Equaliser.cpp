// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "core/Equaliser.h"

#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace ferrolux::core {
namespace {

// SPEC.md §Equaliser. Winamp's ten centres, fixed by D-007 so that existing
// presets map one to one.
constexpr std::array<double, Equaliser::kBandCount> kCentres = {
    60.0, 170.0, 310.0, 600.0, 1000.0, 3000.0, 6000.0, 12000.0, 14000.0, 16000.0
};

// Geometric midpoints to each neighbour, mirrored at the ends. Contiguous
// without overlap; the top three come out narrow because the layout crowds
// there.
std::array<double, Equaliser::kBandCount> computeBandwidths()
{
    std::array<double, Equaliser::kBandCount> widths{};
    for (int i = 0; i < Equaliser::kBandCount; ++i) {
        const double centre = kCentres[size_t(i)];
        double lower = i > 0 ? std::sqrt(kCentres[size_t(i - 1)] * centre) : 0.0;
        double upper = i < Equaliser::kBandCount - 1
                           ? std::sqrt(centre * kCentres[size_t(i + 1)])
                           : 0.0;
        if (lower <= 0.0)
            lower = centre * centre / upper;
        if (upper <= 0.0)
            upper = centre * centre / lower;
        widths[size_t(i)] = upper - lower;
    }
    return widths;
}

// A small bank of named curves. These are Ferrolux's own shaping, not byte
// copies of Winamp's bank: D-006 already establishes that the filter response
// differs from Winamp's, so reproducing its exact numbers would imply a
// fidelity that does not exist. Importing a real `.eqf` is the way to get
// authentic values, and that path is exact.
const QList<QPair<QString, QList<double>>> &presetBank()
{
    static const QList<QPair<QString, QList<double>>> bank = {
        { QStringLiteral("flat"),      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { QStringLiteral("bass"),      { 8, 6, 4, 2, 0, 0, 0, 0, 0, 0 } },
        { QStringLiteral("treble"),    { 0, 0, 0, 0, 0, 2, 4, 6, 7, 7 } },
        { QStringLiteral("loudness"),  { 7, 5, 2, 0, -1, 0, 2, 5, 6, 6 } },
        { QStringLiteral("vocal"),     { -3, -2, 0, 3, 5, 4, 2, 0, -1, -2 } },
        { QStringLiteral("classical"), { 0, 0, 0, 0, 0, 0, -4, -5, -5, -6 } },
        { QStringLiteral("rock"),      { 5, 3, -2, -4, -2, 2, 5, 7, 7, 7 } },
        { QStringLiteral("dance"),     { 6, 5, 2, 0, 0, -3, -4, -4, 0, 0 } },
        { QStringLiteral("soft"),      { 3, 1, 0, -1, -2, 0, 3, 5, 6, 7 } },
    };
    return bank;
}

double toAmplitude(double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

} // namespace

const std::array<double, Equaliser::kBandCount> &Equaliser::centreFrequencies()
{
    return kCentres;
}

const std::array<double, Equaliser::kBandCount> &Equaliser::bandwidths()
{
    static const std::array<double, kBandCount> widths = computeBandwidths();
    return widths;
}

Equaliser::Equaliser(QObject *parent)
    : QObject(parent)
{
    m_bands = QList<double>(kBandCount, 0.0);
}

double Equaliser::band(int index) const
{
    return index >= 0 && index < m_bands.size() ? m_bands.at(index) : 0.0;
}

namespace {

// One RBJ cookbook peaking section, normalised so a0 == 1.
struct Biquad { double b0, b1, b2, a1, a2; };

Biquad peakingSection(double centre, double bandwidth, double gainDb, double rate)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * centre / rate;
    const double q = centre / bandwidth;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cosw = std::cos(w0);

    const double a0 = 1.0 + alpha / A;
    return Biquad{ (1.0 + alpha * A) / a0, (-2.0 * cosw) / a0, (1.0 - alpha * A) / a0,
                   (-2.0 * cosw) / a0, (1.0 - alpha / A) / a0 };
}

double magnitudeAt(const Biquad &section, double omega)
{
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -omega));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> numerator = section.b0 + section.b1 * z1 + section.b2 * z2;
    const std::complex<double> denominator = 1.0 + section.a1 * z1 + section.a2 * z2;
    return std::abs(numerator / denominator);
}

} // namespace

double Equaliser::cascadePeakGain(const QList<double> &bandsDb)
{
    std::vector<Biquad> sections;
    sections.reserve(size_t(bandsDb.size()));

    const auto &widths = bandwidths();
    for (int i = 0; i < kBandCount && i < bandsDb.size(); ++i) {
        if (std::fabs(bandsDb.at(i)) < 1e-9)
            continue; // a flat band is exactly unity and contributes nothing
        sections.push_back(peakingSection(kCentres[size_t(i)], widths[size_t(i)],
                                          bandsDb.at(i), kModelRate));
    }
    if (sections.empty())
        return 0.0;

    // Log-spaced across the audible range. A thousand points resolves the peak
    // to well under a tenth of a decibel, and this runs once per gain change,
    // never per sample.
    constexpr int kPoints = 1024;
    constexpr double kLowest = 20.0;
    constexpr double kHighest = 20000.0;

    double peak = 0.0;
    for (int step = 0; step <= kPoints; ++step) {
        const double fraction = double(step) / kPoints;
        const double frequency =
            kLowest * std::pow(kHighest / kLowest, fraction);
        const double omega = 2.0 * M_PI * frequency / kModelRate;

        double magnitude = 1.0;
        for (const Biquad &section : sections)
            magnitude *= magnitudeAt(section, omega);
        peak = std::max(peak, magnitude);
    }

    return peak > 0.0 ? 20.0 * std::log10(peak) : 0.0;
}

// SPEC.md §Equaliser: attenuation = max(0, preamp_dB + cascade_peak_dB), with a
// 0 dBFS margin. The cascade peak is measured from the curve rather than
// assumed to be the largest band — see BUG-005 and cascadePeakGain().
double Equaliser::headroomAttenuation(double preampDb, const QList<double> &bandsDb)
{
    return std::max(0.0, preampDb + cascadePeakGain(bandsDb));
}

double Equaliser::headroomAttenuation() const
{
    return m_enabled ? headroomAttenuation(m_preamp, m_bands) : 0.0;
}

bool Equaliser::createElements()
{
    // A plain volume element carries the preamp, ahead of the band filters as
    // SPEC.md requires, and absorbs the headroom attenuation with it.
    m_preampElement = gst_element_factory_make("volume", "eq-preamp");
    m_filterElement = gst_element_factory_make("equalizer-nbands", "eq-bands");

    if (!m_preampElement || !m_filterElement) {
        if (m_preampElement)
            gst_object_unref(m_preampElement);
        if (m_filterElement)
            gst_object_unref(m_filterElement);
        m_preampElement = nullptr;
        m_filterElement = nullptr;
        return false;
    }

    g_object_set(m_filterElement, "num-bands", guint(kBandCount), nullptr);
    configureBands();
    applyGains();
    return true;
}

void Equaliser::configureBands()
{
    if (!m_filterElement)
        return;

    const auto &widths = bandwidths();
    for (int i = 0; i < kBandCount; ++i) {
        GObject *child = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(m_filterElement),
                                                            guint(i));
        if (!child)
            continue;
        g_object_set(child,
                     "freq", kCentres[size_t(i)],
                     "bandwidth", widths[size_t(i)],
                     "gain", 0.0,
                     nullptr);
        g_object_unref(child);
    }
}

void Equaliser::applyGains()
{
    if (!m_filterElement || !m_preampElement)
        return;

    const bool on = m_enabled;
    const double attenuation = on ? headroomAttenuation(m_preamp, m_bands) : 0.0;

    // Folding the attenuation into the preamp is exact — gain commutes with a
    // linear filter — and keeps the cascade's internal levels lower, which is
    // where AV-003's instability would begin.
    const double preampDb = on ? m_preamp - attenuation : 0.0;
    g_object_set(m_preampElement, "volume", toAmplitude(preampDb), nullptr);

    for (int i = 0; i < kBandCount; ++i) {
        GObject *child = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(m_filterElement),
                                                            guint(i));
        if (!child)
            continue;
        g_object_set(child, "gain", on ? m_bands.at(i) : 0.0, nullptr);
        g_object_unref(child);
    }

    if (!qFuzzyCompare(attenuation + 1.0, m_appliedAttenuation + 1.0)) {
        m_appliedAttenuation = attenuation;
        emit headroomChanged();
    }
}

void Equaliser::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    applyGains();
    emit enabledChanged();
}

void Equaliser::setPreamp(double decibels)
{
    decibels = qBound(-kPreampLimit, decibels, kPreampLimit);
    if (qFuzzyCompare(decibels + 100.0, m_preamp + 100.0))
        return;
    m_preamp = decibels;
    applyGains();
    emit preampChanged();
}

void Equaliser::setBand(int index, double decibels)
{
    if (index < 0 || index >= m_bands.size())
        return;
    decibels = qBound(-kGainLimit, decibels, kGainLimit);
    if (qFuzzyCompare(decibels + 100.0, m_bands.at(index) + 100.0))
        return;

    m_bands[index] = decibels;
    applyGains();

    if (m_preset != QStringLiteral("custom")) {
        m_preset = QStringLiteral("custom");
        emit presetChanged();
    }
    emit bandsChanged();
}

void Equaliser::setBands(const QList<double> &decibels)
{
    if (decibels.size() != kBandCount)
        return;
    for (int i = 0; i < kBandCount; ++i)
        m_bands[i] = qBound(-kGainLimit, decibels.at(i), kGainLimit);
    applyGains();
    emit bandsChanged();
}

void Equaliser::applyPreset(const QString &name)
{
    const QList<double> values = presetBands(name);
    if (values.size() != kBandCount)
        return;
    setBands(values);
    m_preset = name;
    emit presetChanged();
}

void Equaliser::reset()
{
    setPreamp(0.0);
    applyPreset(QStringLiteral("flat"));
}

QStringList Equaliser::presetNames()
{
    QStringList names;
    for (const auto &entry : presetBank())
        names.append(entry.first);
    return names;
}

QList<double> Equaliser::presetBands(const QString &name)
{
    for (const auto &entry : presetBank()) {
        if (entry.first.compare(name, Qt::CaseInsensitive) == 0)
            return entry.second;
    }
    return {};
}

// SPEC.md §Equaliser: dB = (31 − value) × 12 / 31. Note the inversion — a
// lower stored byte is a *higher* gain — which is the detail that makes a
// naive importer produce a mirror image of the intended curve.
bool Equaliser::decodeEqf(const QByteArray &bytes, QList<double> *bands, double *preamp,
                          QString *name)
{
    // Winamp writes a 31-byte header ("Winamp EQ library file v1.1" plus a
    // terminator), then, per preset, a 257-byte name field and eleven bytes of
    // values. A bare 11-byte payload is also accepted, because that is what
    // most tools that "export an eqf" actually emit.
    constexpr int kHeader = 31;
    constexpr int kName = 257;
    constexpr int kValues = 11;

    const char *values = nullptr;
    if (bytes.size() == kValues) {
        values = bytes.constData();
    } else if (bytes.size() >= kHeader + kName + kValues) {
        if (name)
            *name = QString::fromLatin1(bytes.constData() + kHeader).trimmed();
        values = bytes.constData() + kHeader + kName;
    } else {
        return false;
    }

    QList<double> decoded;
    decoded.reserve(kBandCount);
    for (int i = 0; i < kBandCount; ++i) {
        const int stored = int(quint8(values[i]));
        if (stored > 63)
            return false;
        decoded.append((31.0 - stored) * 12.0 / 31.0);
    }

    const int storedPreamp = int(quint8(values[kBandCount]));
    if (storedPreamp > 63)
        return false;

    if (bands)
        *bands = decoded;
    if (preamp)
        *preamp = (31.0 - storedPreamp) * 12.0 / 31.0;
    return true;
}

bool Equaliser::importEqf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit importFailed(file.errorString());
        return false;
    }

    QList<double> values;
    double preampValue = 0.0;
    QString name;
    if (!decodeEqf(file.readAll(), &values, &preampValue, &name)) {
        emit importFailed(tr("Not a readable Winamp equaliser preset."));
        return false;
    }

    setBands(values);
    setPreamp(preampValue);
    m_preset = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    emit presetChanged();
    return true;
}

} // namespace ferrolux::core
