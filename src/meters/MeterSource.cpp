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
            // lowest 24 bands and twelve of the lowest 48 fall in this gap.
            // The nearest bin is used and the band marked interpolated. No
            // display band may be left empty: an empty band renders as silence
            // and reads as missing bass rather than as a limit of the analysis.
            const double centre = std::sqrt(lower * upper);
            const int nearest = qBound(0, int(std::lround(centre / binWidth - 0.5)),
                                       analysisBins - 1);
            range.first = nearest;
            range.last = nearest;
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

        double loudest = kFloorDb;
        for (int bin = range.first; bin <= range.last && bin < magnitudesDb.size(); ++bin)
            loudest = std::max(loudest, double(magnitudesDb.at(bin)));

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

void MeterSource::consumeLevel(const QList<double> &rmsDb, const QList<double> &peakDb,
                               const QList<double> &decayDb)
{
    Q_UNUSED(peakDb)

    for (int channel = 0; channel < kChannels; ++channel) {
        if (channel < rmsDb.size()) {
            // 0 VU sits at the reference level, so a signal at reference reads
            // 1.0 and the needle rests where the scale says it should.
            const double relativeDb = rmsDb.at(channel) - kVuReferenceDb;
            m_vuTarget[size_t(channel)] = std::pow(10.0, relativeDb / 20.0);
        }
        if (channel < decayDb.size()) {
            // The peak indicator is driven straight from the element's own
            // decaying peak, with its TTL and falloff, and is deliberately not
            // smoothed further: it exists to show what the needle cannot follow.
            m_peakIndicator[size_t(channel)] = normalise(decayDb.at(channel));
        }
    }
}

// Second-order step response, integrated semi-implicitly. Explicit Euler on an
// oscillator gains energy and would slowly wind the needle up; the semi-implicit
// form is stable at the update rates in use. See BUG-010 for why the system is
// second order rather than the first-order one SPEC.md described.
void MeterSource::advance(double elapsedMs)
{
    if (elapsedMs <= 0.0)
        return;

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
    m_spectrumQueue.clear();
    m_levelQueue.clear();
    emit updated();
}

} // namespace ferrolux::meters
