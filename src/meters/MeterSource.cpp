// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "meters/MeterSource.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace ferrolux::meters {
namespace {

constexpr double kLowestHz = 20.0;
constexpr double kHighestHz = 20000.0;

// Maps a decibel magnitude onto 0..1, with the spectrum element's threshold at
// the bottom. Everything downstream of this works in normalised units.
float normalise(double decibels)
{
    const double clamped = qBound(MeterSource::kFloorDb, decibels, 0.0);
    return float((clamped - MeterSource::kFloorDb) / (0.0 - MeterSource::kFloorDb));
}

} // namespace

MeterSource::MeterSource(QObject *parent)
    : QObject(parent)
{
    setBandCount(kSpectrumBands);
}

QStringList MeterSource::modes()
{
    // SPEC.md §Meters. `scope` is F-034 and needs the PCM tap, so it is absent.
    return { QStringLiteral("spectrum"), QStringLiteral("spectrum-mirror"),
             QStringLiteral("flame"), QStringLiteral("vu"), QStringLiteral("ladder") };
}

void MeterSource::setMode(const QString &mode)
{
    if (!modes().contains(mode) || m_mode == mode)
        return;

    m_mode = mode;

    // A mirrored spectrum has half the height per side, so it earns twice the
    // horizontal resolution at the same width. The other modes read the same
    // band data and do not care how finely it is divided.
    // Flame reads the texture as a continuous curve, so it benefits from the
    // finer division the mirrored mode also uses.
    const bool wantsFineBands = mode == QStringLiteral("spectrum-mirror")
                             || mode == QStringLiteral("flame");
    setBandCount(wantsFineBands ? kMirroredBands : kSpectrumBands);
    emit modeChanged();
}

void MeterSource::cycleMode()
{
    const QStringList all = modes();
    const int at = all.indexOf(m_mode);
    setMode(all.at((at + 1) % all.size()));
}

void MeterSource::setBandCount(int bands)
{
    bands = qBound(1, bands, 512);
    if (bands == m_bandCount && !m_magnitudes.isEmpty())
        return;

    m_bandCount = bands;
    m_magnitudes = QList<float>(bands, 0.0f);
    m_peaks = QList<float>(bands, 0.0f);
    m_peakHoldMs = QList<double>(bands, 0.0);
    if (m_analysisBins > 0)
        rebuildRanges(m_analysisBins, m_sampleRate);

    emit bandCountChanged();
}

// SPEC.md §Meters: display bands are spaced logarithmically across 20 Hz to
// 20 kHz, and each takes the *maximum* of the analysis bins whose centres fall
// inside it. Maximum rather than mean, because averaging across a wide upper
// band buries the transients that are the visually interesting part.
QList<MeterSource::BandRange> MeterSource::bandRanges(int displayBands, int analysisBins,
                                                      int sampleRate)
{
    QList<BandRange> ranges;
    if (displayBands <= 0 || analysisBins <= 0 || sampleRate <= 0)
        return ranges;

    ranges.reserve(displayBands);
    const double binWidth = (double(sampleRate) / 2.0) / double(analysisBins);
    const double span = kHighestHz / kLowestHz;

    for (int i = 0; i < displayBands; ++i) {
        const double lower = kLowestHz * std::pow(span, double(i) / displayBands);
        const double upper = kLowestHz * std::pow(span, double(i + 1) / displayBands);

        int first = -1;
        int last = -1;
        for (int bin = 0; bin < analysisBins; ++bin) {
            const double centre = (double(bin) + 0.5) * binWidth;
            if (centre >= lower && centre < upper) {
                if (first < 0)
                    first = bin;
                last = bin;
            }
        }

        BandRange range;
        if (first < 0) {
            // AV-011. At the bottom of the range a display band is narrower
            // than one analysis bin — at 44.1 kHz with 512 bins, four of the
            // lowest 24 bands and twelve of the lowest 48 fall in this gap. No
            // display band may be left empty: an empty band renders as silence
            // and reads as missing bass rather than as a limit of the analysis.
            //
            // The band's centre is recorded in fractional bin coordinates and
            // the magnitude is interpolated between the two bins either side.
            // Rounding to the nearest bin instead makes adjacent starved bands
            // share one value and move as a single welded group.
            const double centre = std::sqrt(lower * upper);
            const double position = qBound(0.0, centre / binWidth - 0.5,
                                           double(analysisBins - 1));
            range.first = int(std::floor(position));
            range.last = qMin(range.first + 1, analysisBins - 1);
            range.centreBin = position;
            range.interpolated = true;
        } else {
            range.first = first;
            range.last = last;
        }
        ranges.append(range);
    }
    return ranges;
}

void MeterSource::rebuildRanges(int analysisBins, int sampleRate)
{
    m_analysisBins = analysisBins;
    m_sampleRate = sampleRate;
    m_ranges = bandRanges(m_bandCount, analysisBins, sampleRate);
}

void MeterSource::consumeSpectrum(const QList<float> &magnitudesDb, int sampleRate)
{
    if (magnitudesDb.isEmpty() || sampleRate <= 0)
        return;

    if (m_analysisBins != magnitudesDb.size() || m_sampleRate != sampleRate)
        rebuildRanges(int(magnitudesDb.size()), sampleRate);

    for (int band = 0; band < m_bandCount && band < m_ranges.size(); ++band) {
        const BandRange &range = m_ranges.at(band);

        double loudest;
        if (range.interpolated) {
            // Linear between the two bins the band's centre falls between, in
            // decibels. Each starved band lands somewhere different, so the
            // lowest bars move independently instead of in lockstep.
            const int low = qBound(0, range.first, int(magnitudesDb.size()) - 1);
            const int high = qBound(0, range.last, int(magnitudesDb.size()) - 1);
            const double fraction = qBound(0.0, range.centreBin - double(range.first), 1.0);
            loudest = double(magnitudesDb.at(low)) * (1.0 - fraction)
                    + double(magnitudesDb.at(high)) * fraction;
        } else {
            // The maximum across the band's bins, not the mean: averaging a
            // wide upper band buries the transients that are the interesting
            // part of a spectrum display.
            loudest = kFloorDb;
            for (int bin = range.first; bin <= range.last && bin < magnitudesDb.size(); ++bin)
                loudest = std::max(loudest, double(magnitudesDb.at(bin)));
        }

        const float target = normalise(loudest);
        const float current = m_magnitudes.at(band);

        // Asymmetric: rises quickly, falls slowly. Symmetric smoothing either
        // smears transients or lets the display flicker.
        const double coefficient = target > current ? kAttack : kRelease;
        m_magnitudes[band] = float(current + (target - current) * coefficient);

        if (m_magnitudes.at(band) >= m_peaks.at(band)) {
            m_peaks[band] = m_magnitudes.at(band);
            m_peakHoldMs[band] = kPeakHoldMs;
        }
    }

    emit updated();
}

void MeterSource::setReferenceLevel(double decibels)
{
    decibels = qBound(-40.0, decibels, 0.0);
    if (qFuzzyCompare(decibels + 100.0, m_referenceDb + 100.0))
        return;
    m_referenceDb = decibels;
    emit referenceLevelChanged();
}

void MeterSource::consumeLevel(const QList<double> &rmsDb, const QList<double> &peakDb,
                               const QList<double> &decayDb)
{
    Q_UNUSED(peakDb)

    for (int channel = 0; channel < kChannels; ++channel) {
        const size_t i = size_t(channel);

        if (channel < rmsDb.size()) {
            // 0 VU sits at the reference level, so a signal at reference reads
            // 1.0 and the needle rests where the scale says it should. The
            // deflection is linear in amplitude, not in decibels, which is what
            // makes a VU scale crowd towards its left end as a real one does.
            const double relativeDb = rmsDb.at(channel) - m_referenceDb;
            m_vuTarget[i] = std::pow(10.0, relativeDb / 20.0);
        }

        if (channel < decayDb.size()) {
            // Peaks get their own decibel scale ending at full scale, not the
            // VU's amplitude ratio. A sample peak runs well above RMS, so
            // sharing the VU reference would peg it on any real material. Not
            // smoothed further either: the element's own TTL and falloff are
            // the ballistic, and the indicator exists to show what the needle
            // cannot follow.
            const double clamped = qBound(kPeakFloorDb, decayDb.at(channel), 0.0);
            m_peakIndicator[i] = (clamped - kPeakFloorDb) / (0.0 - kPeakFloorDb);
        }
    }
}

// Second-order step response, integrated semi-implicitly. Explicit Euler on an
// oscillator gains energy and would slowly wind the needle up; the semi-implicit
// form is stable at the update rates in use. See BUG-010 for why the system is
// second order rather than the first-order one SPEC.md described.
void MeterSource::setReleasing(bool releasing)
{
    if (m_releasing == releasing)
        return;
    m_releasing = releasing;

    if (m_releasing) {
        // Anything still queued describes audio that will never be heard now.
        m_spectrumQueue.clear();
        m_levelQueue.clear();

        // Aim the needles at rest and let the ballistic carry them there. A
        // needle that snaps to zero looks like a meter being switched off; one
        // that falls at its own rate looks like a meter that has stopped being
        // driven, which is what has happened.
        m_vuTarget.fill(0.0);
    }
}

void MeterSource::advance(double elapsedMs)
{
    if (elapsedMs <= 0.0)
        return;

    // Falling back to rest. The spectrum uses its own release coefficient, so
    // the bars settle at the rate they would settle at anyway — the display
    // does not acquire a second, different decay for this one case.
    if (m_releasing) {
        const double steps = elapsedMs / 16.0;
        const double factor = std::pow(1.0 - kRelease, std::max(0.0, steps));
        for (int band = 0; band < m_magnitudes.size(); ++band)
            m_magnitudes[band] = float(double(m_magnitudes.at(band)) * factor);
        for (int channel = 0; channel < kChannels; ++channel)
            m_peakIndicator[size_t(channel)] *= factor;
    }

    const double dt = elapsedMs / 1000.0;
    const double wn = kVuNaturalFrequency;
    const double zeta = kVuDamping;

    for (int channel = 0; channel < kChannels; ++channel) {
        const size_t i = size_t(channel);
        const double error = m_vuTarget[i] - m_vu[i];
        m_vuVelocity[i] += (wn * wn * error - 2.0 * zeta * wn * m_vuVelocity[i]) * dt;
        m_vu[i] += m_vuVelocity[i] * dt;
        if (m_vu[i] < 0.0) {
            m_vu[i] = 0.0;
            m_vuVelocity[i] = 0.0;
        }
    }

    // Per-channel hold, tracking the deflection the ladder actually shows. Held
    // after the needle has moved this step, so the mark can never lag behind
    // the run it is meant to sit above.
    const double channelFallPerMs = (kPeakFallDbPerSecond / 1000.0) / 20.0;
    for (int channel = 0; channel < kChannels; ++channel) {
        const size_t i = size_t(channel);
        if (m_vu[i] >= m_channelPeak[i]) {
            m_channelPeak[i] = m_vu[i];
            m_channelPeakHoldMs[i] = kPeakHoldMs;
            continue;
        }
        if (m_channelPeakHoldMs[i] > 0.0) {
            m_channelPeakHoldMs[i] -= elapsedMs;
            continue;
        }
        m_channelPeak[i] = std::max(m_vu[i], m_channelPeak[i] - channelFallPerMs * elapsedMs);
    }

    // Peak-hold caps: hold, then fall at a fixed rate in decibels per second,
    // converted into the normalised scale the texture carries.
    const double fallPerMs = (kPeakFallDbPerSecond / 1000.0) / (0.0 - kFloorDb);
    for (int band = 0; band < m_peaks.size(); ++band) {
        if (m_peakHoldMs.at(band) > 0.0) {
            m_peakHoldMs[band] -= elapsedMs;
            continue;
        }
        m_peaks[band] = float(std::max(double(m_magnitudes.at(band)),
                                       double(m_peaks.at(band)) - fallPerMs * elapsedMs));
    }

    // Ballistics and decay both change what is on screen, so this is a display
    // update in its own right and not only consumeSpectrum's business.
    emit updated();
}

void MeterSource::queueSpectrum(const QList<float> &magnitudesDb, int sampleRate,
                                qint64 runningTimeNs)
{
    if (magnitudesDb.isEmpty() || sampleRate <= 0)
        return;
    if (m_spectrumQueue.size() >= kMaxQueuedFrames)
        m_spectrumQueue.removeFirst(); // drop the oldest; it is already late
    m_spectrumQueue.append(SpectrumFrame{ magnitudesDb, sampleRate, runningTimeNs });
}

void MeterSource::queueLevel(const QList<double> &rmsDb, const QList<double> &peakDb,
                             const QList<double> &decayDb, qint64 runningTimeNs)
{
    if (m_levelQueue.size() >= kMaxQueuedFrames)
        m_levelQueue.removeFirst();
    m_levelQueue.append(LevelFrame{ rmsDb, peakDb, decayDb, runningTimeNs });
}

// Applies every frame whose moment has arrived, in order. All of them, not just
// the newest: the smoothing coefficients in SPEC.md §Meters are defined per
// update interval, so skipping frames would make the display settle faster than
// specified and quietly undo the ballistics.
void MeterSource::releaseUpTo(qint64 runningTimeNs)
{
    if (runningTimeNs < 0)
        return;

    while (!m_levelQueue.isEmpty() && m_levelQueue.first().runningTimeNs <= runningTimeNs) {
        const LevelFrame frame = m_levelQueue.takeFirst();
        consumeLevel(frame.rms, frame.peak, frame.decay);
    }

    while (!m_spectrumQueue.isEmpty() && m_spectrumQueue.first().runningTimeNs <= runningTimeNs) {
        const SpectrumFrame frame = m_spectrumQueue.takeFirst();
        consumeSpectrum(frame.magnitudes, frame.sampleRate);
    }
}

QList<double> MeterSource::vuLevels() const
{
    QList<double> values;
    values.reserve(kChannels);
    for (int channel = 0; channel < kChannels; ++channel)
        values.append(m_vu[size_t(channel)]);
    return values;
}

QList<double> MeterSource::peakIndicatorLevels() const
{
    QList<double> values;
    values.reserve(kChannels);
    for (int channel = 0; channel < kChannels; ++channel)
        values.append(m_peakIndicator[size_t(channel)]);
    return values;
}

double MeterSource::vuDeflection(int channel) const
{
    return channel >= 0 && channel < kChannels ? m_vu[size_t(channel)] : 0.0;
}

QList<double> MeterSource::channelPeakLevels() const
{
    QList<double> values;
    values.reserve(kChannels);
    for (int channel = 0; channel < kChannels; ++channel)
        values.append(m_channelPeak[size_t(channel)]);
    return values;
}

double MeterSource::peakIndicator(int channel) const
{
    return channel >= 0 && channel < kChannels ? m_peakIndicator[size_t(channel)] : 0.0;
}

void MeterSource::reset()
{
    m_magnitudes.fill(0.0f);
    m_peaks.fill(0.0f);
    m_peakHoldMs.fill(0.0);
    m_vu.fill(0.0);
    m_vuVelocity.fill(0.0);
    m_vuTarget.fill(0.0);
    m_peakIndicator.fill(0.0);
    m_channelPeak.fill(0.0);
    m_channelPeakHoldMs.fill(0.0);
    m_spectrumQueue.clear();
    m_levelQueue.clear();
    emit updated();
}

} // namespace ferrolux::meters
