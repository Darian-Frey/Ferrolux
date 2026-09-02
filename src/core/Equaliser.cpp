// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "core/Equaliser.h"

#include <QFile>
#include <QSettings>
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
    m_writtenBands = QList<double>(kBandCount, 0.0);
    m_rampFromBands = QList<double>(kBandCount, 0.0);

    // Five milliseconds is finer than the element can actually act on — it
    // re-reads the gain once per buffer, which is nearer ten — but timing the
    // ramp from the clock rather than from the tick count means jitter in the
    // timer does not stretch or shorten it.
    m_rampTimer.setInterval(5);
    m_rampTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_rampTimer, &QTimer::timeout, this, &Equaliser::stepRamp);
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

// SPEC.md §Equaliser. Reported so that the interface can warn, never subtracted
// from the signal — subtracting it is what made the equaliser cut-only in
// BUG-008. The cascade peak is measured from the curve rather than assumed to
// be the largest band; see BUG-005 and cascadePeakGain().
double Equaliser::excessGain(double preampDb, const QList<double> &bandsDb)
{
    return std::max(0.0, preampDb + cascadePeakGain(bandsDb));
}

double Equaliser::excessGain() const
{
    return m_enabled ? excessGain(m_preamp, m_bands) : 0.0;
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

QList<double> Equaliser::targetBands() const
{
    return m_enabled ? m_bands : QList<double>(kBandCount, 0.0);
}

// The preamp is exactly what the user asked for. Nothing is subtracted from it:
// the automatic attenuation that used to live here cancelled every boost in the
// signal path, which is BUG-008.
double Equaliser::targetPreampDb() const
{
    return m_enabled ? m_preamp : 0.0;
}

void Equaliser::writeGains(const QList<double> &bandsDb, double preampDb)
{
    if (!m_filterElement || !m_preampElement)
        return;

    g_object_set(m_preampElement, "volume", toAmplitude(preampDb), nullptr);

    for (int i = 0; i < kBandCount && i < bandsDb.size(); ++i) {
        GObject *child = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(m_filterElement),
                                                            guint(i));
        if (!child)
            continue;
        g_object_set(child, "gain", bandsDb.at(i), nullptr);
        g_object_unref(child);
    }

    m_writtenBands = bandsDb;
    m_writtenPreampDb = preampDb;
}

void Equaliser::stepRamp()
{
    const double elapsed = double(m_rampClock.elapsed());
    const double fraction = qBound(0.0, elapsed / double(kRampMilliseconds), 1.0);

    const QList<double> to = targetBands();
    const double toPreamp = targetPreampDb();

    QList<double> stepped;
    stepped.reserve(kBandCount);
    for (int i = 0; i < kBandCount; ++i) {
        const double from = m_rampFromBands.value(i, 0.0);
        stepped.append(from + (to.value(i, 0.0) - from) * fraction);
    }
    writeGains(stepped, m_rampFromPreampDb + (toPreamp - m_rampFromPreampDb) * fraction);

    if (fraction >= 1.0) {
        m_rampTimer.stop();
        writeGains(to, toPreamp); // land exactly on the target, not near it
    }
}

void Equaliser::applyGains()
{
    if (!m_filterElement || !m_preampElement)
        return;

    const double excess = excessGain();
    if (!qFuzzyCompare(excess + 1.0, m_reportedExcess + 1.0)) {
        m_reportedExcess = excess;
        emit excessGainChanged();
    }

    // The first write after the elements are built is the initial state, not a
    // change a listener could hear, so it is applied whole. Ramping it would
    // also mean the first 30 ms of any capture disagreed with the settings,
    // which would quietly weaken the tests that measure worst-case gain.
    if (!m_elementsPrimed) {
        m_elementsPrimed = true;
        writeGains(targetBands(), targetPreampDb());
        return;
    }

    // A change that changes nothing — enabling a flat curve, or re-applying the
    // preset already loaded — has nothing to interpolate, and starting a timer
    // for it would leave the equaliser reporting itself busy for 30 ms with no
    // audible event to justify it.
    const QList<double> to = targetBands();
    const double toPreamp = targetPreampDb();
    bool identical = qFuzzyCompare(toPreamp + 100.0, m_writtenPreampDb + 100.0);
    for (int i = 0; identical && i < kBandCount; ++i)
        identical = qFuzzyCompare(to.value(i) + 100.0, m_writtenBands.value(i) + 100.0);
    if (identical)
        return;

    m_rampFromBands = m_writtenBands;
    m_rampFromPreampDb = m_writtenPreampDb;
    m_rampClock.restart();
    if (!m_rampTimer.isActive())
        m_rampTimer.start();
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
    // A user preset of the same name wins: it was saved deliberately, and
    // shadowing a built-in is a reasonable thing to want.
    QSettings settings;
    const QVariant stored = settings.value(QStringLiteral("equaliser/user/") + name);
    if (stored.isValid()) {
        QList<double> values;
        for (const QVariant &value : stored.toList())
            values.append(value.toDouble());
        if (values.size() == kBandCount + 1) {
            applyCurve(values, name);
            return;
        }
    }

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

QList<double> Equaliser::curve() const
{
    QList<double> values = m_bands;
    values.append(m_preamp);
    return values;
}

void Equaliser::applyCurve(const QList<double> &values, const QString &name)
{
    if (values.size() != kBandCount + 1)
        return;
    setBands(values.mid(0, kBandCount));
    setPreamp(values.at(kBandCount));
    m_preset = name.isEmpty() ? QStringLiteral("custom") : name;
    emit presetChanged();
}

QStringList Equaliser::bandLabels()
{
    QStringList labels;
    for (double centre : kCentres) {
        labels.append(centre >= 1000.0
                          ? QStringLiteral("%1k").arg(centre / 1000.0, 0, 'g', 2)
                          : QString::number(int(centre)));
    }
    return labels;
}

QStringList Equaliser::availablePresets() const
{
    QStringList names = presetNames();
    for (const QString &user : userPresetNames()) {
        if (!names.contains(user, Qt::CaseInsensitive))
            names.append(user);
    }
    return names;
}

QStringList Equaliser::userPresetNames() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("equaliser/user"));
    QStringList names = settings.childKeys();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool Equaliser::saveUserPreset(const QString &name)
{
    const QString trimmed = name.trimmed();
    // A slash would open a settings subgroup rather than name a preset, and an
    // empty name would produce an unreachable entry.
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')))
        return false;

    QVariantList stored;
    for (double value : curve())
        stored.append(value);

    QSettings settings;
    settings.setValue(QStringLiteral("equaliser/user/") + trimmed, stored);

    m_preset = trimmed;
    emit presetChanged();
    emit userPresetsChanged();
    return true;
}

bool Equaliser::removeUserPreset(const QString &name)
{
    QSettings settings;
    const QString key = QStringLiteral("equaliser/user/") + name;
    if (!settings.contains(key))
        return false;
    settings.remove(key);
    emit userPresetsChanged();
    return true;
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

bool Equaliser::importEqf(const QUrl &fileUrl)
{
    // Takes a URL rather than a path so that a file chooser's selection can be
    // passed straight through, as PlaylistModel::loadFrom does.
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
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
