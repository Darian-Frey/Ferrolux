// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// core/Equaliser.h — ten-band graphic equaliser.
//
// Delivers F-020 through F-022. Per D-006 this is a thin abstraction over a
// stock GStreamer element that exposes gains in decibels and nothing else: no
// backend property, element name or unit escapes this class, so the filter can
// later be replaced by a hand-written biquad cascade without the UI or the
// preset code noticing. That seam is load-bearing and must not be leaked
// through.
//
// Backend
// -------
// `equalizer-nbands` with ten child bands, not `equalizer-10bands`. The latter
// is named in SPEC.md but its centre frequencies are fixed at 29 Hz through
// 15 kHz and cannot be changed, so it cannot deliver the Winamp centres that
// D-007 fixes for preset compatibility. See BUG-004.
//
// Headroom
// --------
// Ten bands at +12 dB with a +12 dB preamp will clip a loud master, and
// clipping inside an IIR cascade can drive the filters unstable rather than
// merely distorting (AV-003). SPEC.md §Equaliser therefore specifies an
// automatic attenuation of max(0, preamp + max_band).
//
// That attenuation is folded into the preamp stage rather than applied after
// the filters. Gain commutes with a linear filter, so (preamp − attenuation)
// ahead of the cascade is mathematically identical to preamp ahead and
// attenuation behind — and it is strictly better in two ways: the cascade's
// internal state stays smaller, which is where instability would start, and
// `level` and `spectrum` downstream see the signal as it is actually heard.

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>

#include <array>

typedef struct _GstElement GstElement;

namespace ferrolux::core {

class Equaliser : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY preampChanged)
    Q_PROPERTY(QList<double> bands READ bands NOTIFY bandsChanged)
    Q_PROPERTY(QString preset READ preset NOTIFY presetChanged)

public:
    static constexpr int kBandCount = 10;
    static constexpr double kGainLimit = 12.0;   // ±dB, per D-007
    static constexpr double kPreampLimit = 12.0;

    // SPEC.md §Equaliser: gains are interpolated over 30 ms rather than jumped,
    // so that dragging a slider does not produce zipper noise. Marked
    // provisional there and still provisional here.
    //
    // Driven by a timer on this thread rather than by a GStreamer control
    // source. `equalizer-nbands` advertises its band gains as controllable and
    // a binding attaches without error, but the element never calls
    // gst_object_sync_values on its bands while streaming, so a bound control
    // source is silently inert — verified by comparing a bound source during
    // playback (no movement at all) against manual synchronisation of the same
    // source (exact linear interpolation). See BUG-006.
    static constexpr int kRampMilliseconds = 30;

    // SPEC.md §Equaliser. Centres are Winamp's; bandwidths are the geometric
    // midpoints between neighbours, which makes the bands contiguous without
    // overlapping. The top three are necessarily narrow and high-Q because
    // 12 k, 14 k and 16 k sit close together — a property of the layout D-007
    // inherits deliberately, not of this implementation.
    static const std::array<double, kBandCount> &centreFrequencies();
    static const std::array<double, kBandCount> &bandwidths();

    // Short labels for the ten centres, for use as control legends.
    Q_INVOKABLE static QStringList bandLabels();

    explicit Equaliser(QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    double preamp() const { return m_preamp; }
    QList<double> bands() const { return m_bands; }
    QString preset() const { return m_preset; }
    Q_INVOKABLE double band(int index) const;

    // The gains currently written to the element, which during a ramp are
    // between the old curve and the new one. bands() reports what the user
    // asked for; this reports what is actually being applied.
    QList<double> appliedBands() const { return m_writtenBands; }
    double appliedPreampDb() const { return m_writtenPreampDb; }
    bool isRamping() const { return m_rampTimer.isActive(); }

    // Peak magnitude of the whole band cascade, in dB, for a given curve.
    //
    // Not the largest single band gain. Ten peaking sections in series multiply
    // where their skirts overlap, and the Winamp centres overlap a great deal:
    // all ten at +12 dB peaks at +21.4 dB near 607 Hz, not +12 dB. Assuming
    // otherwise was BUG-005, which clipped by 8 dB.
    //
    // Evaluated at kModelRate rather than the negotiated rate. The response
    // grows slightly with sample rate — 21.4 dB at 44.1 kHz against 23.7 dB at
    // 192 kHz for the worst curve — so modelling the highest supported rate is
    // conservative at every lower one and avoids having to know what the
    // pipeline negotiated.
    static constexpr double kModelRate = 192000.0;
    static double cascadePeakGain(const QList<double> &bandsDb);

    // How far the current settings can push the signal above unity, in dB.
    //
    // Reported, not applied. An earlier design attenuated by exactly this
    // amount so that output could never exceed full scale, which made the
    // equaliser incapable of boosting anything: the attenuation cancelled the
    // gain that caused it, so raising a band merely lowered everything else and
    // raising the preamp did nothing at all. See BUG-008.
    //
    // The manual preamp is the control for level, as it is in every comparable
    // player. AV-003's concern was that clipping could drive the filters
    // unstable; since BUG-007 the chain runs in F32LE, where levels above unity
    // are ordinary, so what remains is clipping at the sink conversion — the
    // user's business, and visible through this figure.
    static double excessGain(double preampDb, const QList<double> &bandsDb);
    Q_INVOKABLE double excessGain() const;

    // Winamp `.eqf` (F-022). Eleven bytes per preset — ten bands then the
    // preamp — each 0 to 63 with 31 meaning 0 dB, and the scale inverted, so
    // that a lower stored value is a higher gain. SPEC.md §Equaliser.
    static bool decodeEqf(const QByteArray &bytes, QList<double> *bands,
                          double *preamp, QString *name = nullptr);

    static QStringList presetNames();
    static QList<double> presetBands(const QString &name);

    // User presets (F-022). Persisted under the key shape in SPEC.md §Settings.
    //
    // Stored here rather than in platform/ because there is no platform/ yet;
    // main.cpp holds the playback settings for the same reason. Both move to
    // platform/Settings in Phase 6, which is the one place this knowledge
    // should end up.
    // Built-ins and user presets together, which is what a chooser wants. A user
    // preset shadowing a built-in appears once.
    Q_INVOKABLE QStringList availablePresets() const;
    Q_INVOKABLE QStringList userPresetNames() const;
    Q_INVOKABLE bool saveUserPreset(const QString &name);
    Q_INVOKABLE bool removeUserPreset(const QString &name);

    // The full curve as eleven values — ten band gains then the preamp — which
    // is the shape both `.eqf` and the settings store use.
    QList<double> curve() const;
    void applyCurve(const QList<double> &values, const QString &name = {});

    // Backend plumbing. Both elements are created here and handed to Engine to
    // be added to the filter bin; ownership passes with them. Confined to
    // core/ per ARCHITECTURE.md invariant 2.
    bool createElements();
    GstElement *preampElement() const { return m_preampElement; }
    GstElement *filterElement() const { return m_filterElement; }

public slots:
    // Bypass writes unity to every band and zero to the preamp rather than
    // removing the element, so that toggling cannot cause a graph rebuild —
    // ARCHITECTURE.md invariant 7. F-020 requires the result to be
    // bit-identical to the unprocessed signal.
    void setEnabled(bool enabled);

    void setPreamp(double decibels);
    void setBand(int index, double decibels);
    void setBands(const QList<double> &decibels);
    void applyPreset(const QString &name);
    void reset();
    bool importEqf(const QUrl &fileUrl);

signals:
    void enabledChanged();
    void userPresetsChanged();
    void preampChanged();
    void bandsChanged();
    void presetChanged();
    void excessGainChanged();
    void importFailed(const QString &message);

private:
    void applyGains();
    void configureBands();
    void writeGains(const QList<double> &bandsDb, double preampDb);
    void stepRamp();

    // Target gains for the current settings: the band curve and the effective
    // preamp, which carries the headroom attenuation folded in.
    QList<double> targetBands() const;
    double targetPreampDb() const;

    GstElement *m_preampElement = nullptr;
    GstElement *m_filterElement = nullptr;

    bool m_enabled = false;
    double m_preamp = 0.0;
    QList<double> m_bands;
    QString m_preset = QStringLiteral("flat");
    double m_reportedExcess = 0.0;

    // Ramp state. m_written is what the elements currently hold, which during a
    // ramp is an interpolated value rather than either endpoint.
    bool m_elementsPrimed = false;
    QList<double> m_writtenBands;
    double m_writtenPreampDb = 0.0;
    QList<double> m_rampFromBands;
    double m_rampFromPreampDb = 0.0;
    QElapsedTimer m_rampClock;
    QTimer m_rampTimer;
};

} // namespace ferrolux::core
