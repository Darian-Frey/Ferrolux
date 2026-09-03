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
#include <QString>
#include <QStringList>
#include <QObject>

#include <array>

namespace ferrolux::meters {

class MeterSource : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int bandCount READ bandCount NOTIFY bandCountChanged)
    // The display mode lives here rather than in the view because it decides
    // the band count: a mirrored spectrum wants twice the resolution of an
    // upright one at the same width. Identifiers are those in SPEC.md §Meters.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QList<float> magnitudes READ magnitudes NOTIFY updated)
    Q_PROPERTY(QList<float> peaks READ peaks NOTIFY updated)
    Q_PROPERTY(double ceiling READ ceiling NOTIFY updated)
    Q_PROPERTY(int queueDepth READ queueDepth NOTIFY updated)
    // Needle deflection per channel, as a property rather than only a method.
    // A QML binding over a plain function call has nothing to depend on, so it
    // evaluates once and never again — which is why the readout sat at 0.000
    // while the bars, bound to a notifying property, moved correctly.
    Q_PROPERTY(QList<double> vu READ vuLevels NOTIFY updated)
    Q_PROPERTY(QList<double> peakIndicators READ peakIndicatorLevels NOTIFY updated)
    Q_PROPERTY(QList<double> channelPeaks READ channelPeakLevels NOTIFY updated)
    Q_PROPERTY(double referenceLevel READ referenceLevel WRITE setReferenceLevel NOTIFY referenceLevelChanged)

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

    // 0 VU, in dBFS. Configurable, and SPEC.md §Settings always said it should
    // be: it is an alignment convention, not a property of the signal.
    //
    // -9 rather than the -18 broadcast figure originally specified. EBU
    // alignment assumes programme far quieter than a consumer master. Measured
    // across six ordinary tracks: sustained RMS runs -10 to -22 dBFS, mostly
    // near -15, with loud passages reaching -6.3. Against -18 that pegs the
    // needle before the music starts; against -9 the same material sits around
    // half deflection and its loudest moments land at the top of the travel,
    // which is the span a VU meter exists to show. See BUG-014.
    static constexpr double kDefaultReferenceDb = -9.0;

    // Peak metering is a different scale from the VU and must not share one. A
    // sample peak legitimately runs ten decibels or more above RMS, so mapping
    // it against the VU reference pegs it permanently. Peaks are shown on a
    // decibel scale ending at full scale, as every hardware peak meter does.
    static constexpr double kPeakFloorDb = -60.0;

    explicit MeterSource(QObject *parent = nullptr);

    int bandCount() const { return m_bandCount; }
    void setBandCount(int bands);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE static QStringList modes();

    // Display-ready state, all normalised 0..1 except the VU deflection, which
    // is in VU units where 1.0 is 0 VU and may exceed 1.0 on peaks.
    const QList<float> &magnitudes() const { return m_magnitudes; }
    const QList<float> &peaks() const { return m_peaks; }

    // The tallest smoothed band in the current frame, 0 to 1.
    //
    // A bound rather than a datum: the flame shader draws nine receding
    // silhouettes and no band exceeds this, so one comparison against it
    // dismisses a pixel that no rank can reach and saves the forty-five texture
    // taps that would have established the same thing rank by rank. It travels
    // as a uniform because the texture cannot carry it — see the note in
    // MeterTexture::encodeTexel, and BUG-016.
    double ceiling() const;
    Q_INVOKABLE double vuDeflection(int channel) const;
    QList<double> vuLevels() const;
    QList<double> peakIndicatorLevels() const;

    // Held maximum of the *deflection* per channel, with the same hold and decay
    // as the spectrum caps. The ladder marks this above its lit run.
    //
    // On the VU scale, not the peak one. A ladder driven by sample peak sits
    // near full on any modern master — peaks really are within a few decibels
    // of full scale, so that reading is correct and tells the viewer nothing.
    // Driven by the ballistic level it moves with the music, and its hot
    // threshold can mean something: above 0 VU.
    QList<double> channelPeakLevels() const;

    double referenceLevel() const { return m_referenceDb; }
    void setReferenceLevel(double decibels);
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

    // Whether the display is falling back to rest.
    //
    // Stopping and pausing are different events and the meters should say so.
    // A paused deck holds its needles where the music left them — the signal
    // has not gone away, it is suspended. A stopped one has nothing to show,
    // and its needles fall back under their own ballistic rather than snapping
    // to zero, because that fall is the same physical system as the rise.
    Q_INVOKABLE void setReleasing(bool releasing);
    bool isReleasing() const { return m_releasing; }

    // Which analysis bins feed a display band, for a given band count and rate.
    // Exposed so AV-011 can be tested directly rather than inferred from a
    // rendered picture.
    struct BandRange {
        int first = 0;
        int last = 0;
        bool interpolated = false; // spans less than one analysis bin
        // Where the band's centre falls in bin coordinates, for the
        // interpolated case. Fractional on purpose: taking the *nearest* bin
        // makes several starved bands share one value and move in lockstep,
        // which is visible as a group of bars welded together at the bottom of
        // the display. Interpolating between neighbours gives each its own
        // value, which is what "interpolated" was always supposed to mean.
        double centreBin = 0.0;
    };
    static QList<BandRange> bandRanges(int displayBands, int analysisBins, int sampleRate);

signals:
    void bandCountChanged();
    void modeChanged();
    void referenceLevelChanged();
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

    bool m_releasing = false;
    QList<SpectrumFrame> m_spectrumQueue;
    QList<LevelFrame> m_levelQueue;

    int m_bandCount = kSpectrumBands;
    QString m_mode = QStringLiteral("spectrum");
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
    std::array<double, kChannels> m_channelPeak {};
    std::array<double, kChannels> m_channelPeakHoldMs {};
    double m_referenceDb = kDefaultReferenceDb;
};

} // namespace ferrolux::meters
