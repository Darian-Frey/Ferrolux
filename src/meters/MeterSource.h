// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// meters/MeterSource.h — turns bus traffic into display-ready meter state.
//
// Delivers F-030. Per D-005 everything time-dependent happens here, on the CPU,
// where it can be tested without rendering anything: bucketing, smoothing,
// peak-hold and the VU ballistics. The GPU draws; it does not decide.
//
// Threading
// ---------
// Fed from GStreamer bus messages, which arrive on the main loop rather than on
// a streaming thread — see ARCHITECTURE.md §Key invariants item 1 and AV-001.
// Nothing here may be called from a streaming thread, and nothing here touches
// the scene graph; MeterTexture handles GPU residency separately (AV-007).
//
// AV-012
// ------
// The VU and spectrum paths share this object, which is exactly how a smoothing
// change made for one silently changes the other. They are kept as separate
// state with separate constants for that reason, and are tested separately.

#pragma once

#include <QList>
#include <QObject>

#include <array>

namespace ferrolux::meters {

class MeterSource : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int bandCount READ bandCount NOTIFY bandCountChanged)
    Q_PROPERTY(QList<float> magnitudes READ magnitudes NOTIFY updated)
    Q_PROPERTY(QList<float> peaks READ peaks NOTIFY updated)
    Q_PROPERTY(int queueDepth READ queueDepth NOTIFY updated)
    // Needle deflection per channel, as a property rather than only a method.
    // A QML binding over a plain function call has nothing to depend on, so it
    // evaluates once and never again — which is why the readout sat at 0.000
    // while the bars, bound to a notifying property, moved correctly.
    Q_PROPERTY(QList<double> vu READ vuLevels NOTIFY updated)
    Q_PROPERTY(QList<double> peakIndicators READ peakIndicatorLevels NOTIFY updated)

public:
    // SPEC.md §Meters. Display band counts are provisional, chosen for
    // legibility at typical panel widths.
    static constexpr int kSpectrumBands = 24;
    static constexpr int kMirroredBands = 48;
    static constexpr int kChannels = 2;

    // Normalisation: the spectrum element's threshold maps to 0.0 and 0 dBFS
    // to 1.0, so everything downstream works in 0..1 and the shaders never see
    // a decibel.
    static constexpr double kFloorDb = -80.0;

    // Asymmetric exponential smoothing per 16 ms update. Fast rise, slow fall,
    // which is what makes a spectrum readable rather than frantic.
    static constexpr double kAttack = 0.6;
    static constexpr double kRelease = 0.15;

    // Peak-hold: rises instantly to any new maximum, holds, then decays.
    static constexpr double kPeakHoldMs = 1500.0;
    static constexpr double kPeakFallDbPerSecond = 20.0;

    // VU ballistics, IEC 60268-17: 99% of full deflection at 300 ms with 1% to
    // 1.5% overshoot. A **second-order** system — a first-order one is
    // monotonic and cannot overshoot at all, which is what SPEC.md asked for
    // and BUG-010 records. These values are the 1.25% midpoint, solved for the
    // 99%-at-300 ms condition.
    static constexpr double kVuDamping = 0.8127;
    static constexpr double kVuNaturalFrequency = 13.512; // rad/s
    static constexpr double kVuReferenceDb = -18.0;       // 0 VU, SPEC.md §Settings

    explicit MeterSource(QObject *parent = nullptr);

    int bandCount() const { return m_bandCount; }
    void setBandCount(int bands);

    // Display-ready state, all normalised 0..1 except the VU deflection, which
    // is in VU units where 1.0 is 0 VU and may exceed 1.0 on peaks.
    const QList<float> &magnitudes() const { return m_magnitudes; }
    const QList<float> &peaks() const { return m_peaks; }
    Q_INVOKABLE double vuDeflection(int channel) const;
    QList<double> vuLevels() const;
    QList<double> peakIndicatorLevels() const;
    Q_INVOKABLE double peakIndicator(int channel) const;

    // Analysis input, applied immediately. Both take the values a GStreamer
    // element message carries: magnitudes in dB from `spectrum`, and
    // per-channel dB from `level`.
    void consumeSpectrum(const QList<float> &magnitudesDb, int sampleRate);
    void consumeLevel(const QList<double> &rmsDb, const QList<double> &peakDb,
                      const QList<double> &decayDb);

    // Scheduled input. The analysis elements sit upstream of the sink and only
    // the sink synchronises to the clock, so messages arrive well over a second
    // before the audio they describe — measured at 1307 ms mean, 1412 ms worst.
    // Applying them on arrival would show a transient long before it could be
    // heard. Frames are held against the running time they carry and released
    // when the pipeline's clock reaches them. See BUG-011.
    void queueSpectrum(const QList<float> &magnitudesDb, int sampleRate, qint64 runningTimeNs);
    void queueLevel(const QList<double> &rmsDb, const QList<double> &peakDb,
                    const QList<double> &decayDb, qint64 runningTimeNs);
    Q_INVOKABLE void releaseUpTo(qint64 runningTimeNs);

    // Frames held but not yet due. Exposed so AV-004's queue-depth concern can
    // be observed rather than guessed at.
    int queueDepth() const { return int(m_spectrumQueue.size() + m_levelQueue.size()); }

    // Bounded so that a stalled clock cannot grow the queue without limit.
    // About 1.5 seconds of frames at the configured interval, which is the
    // observed worst-case lead with a little room.
    static constexpr int kMaxQueuedFrames = 128;

    // Advances every time-dependent quantity by one interval. Separate from the
    // consume calls so that decay continues when messages stop arriving — a
    // paused pipeline must not freeze a peak-hold cap on screen for ever.
    Q_INVOKABLE void advance(double elapsedMs);

    void reset();

    // Which analysis bins feed a display band, for a given band count and rate.
    // Exposed so AV-011 can be tested directly rather than inferred from a
    // rendered picture.
    struct BandRange {
        int first = 0;
        int last = 0;
        bool interpolated = false; // spans less than one analysis bin
    };
    static QList<BandRange> bandRanges(int displayBands, int analysisBins, int sampleRate);

signals:
    void bandCountChanged();
    void updated();

private:
    struct SpectrumFrame {
        QList<float> magnitudes;
        int sampleRate = 0;
        qint64 runningTimeNs = 0;
    };
    struct LevelFrame {
        QList<double> rms;
        QList<double> peak;
        QList<double> decay;
        qint64 runningTimeNs = 0;
    };

    void rebuildRanges(int analysisBins, int sampleRate);

    QList<SpectrumFrame> m_spectrumQueue;
    QList<LevelFrame> m_levelQueue;

    int m_bandCount = kSpectrumBands;
    int m_analysisBins = 0;
    int m_sampleRate = 0;
    QList<BandRange> m_ranges;

    QList<float> m_magnitudes;
    QList<float> m_peaks;
    QList<double> m_peakHoldMs;

    // Second-order VU state per channel: deflection and its rate of change.
    std::array<double, kChannels> m_vu {};
    std::array<double, kChannels> m_vuVelocity {};
    std::array<double, kChannels> m_vuTarget {};
    std::array<double, kChannels> m_peakIndicator {};
};

} // namespace ferrolux::meters
