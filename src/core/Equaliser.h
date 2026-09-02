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

#include <QList>
#include <QObject>
#include <QString>

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

    // SPEC.md §Equaliser. Centres are Winamp's; bandwidths are the geometric
    // midpoints between neighbours, which makes the bands contiguous without
    // overlapping. The top three are necessarily narrow and high-Q because
    // 12 k, 14 k and 16 k sit close together — a property of the layout D-007
    // inherits deliberately, not of this implementation.
    static const std::array<double, kBandCount> &centreFrequencies();
    static const std::array<double, kBandCount> &bandwidths();

    explicit Equaliser(QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    double preamp() const { return m_preamp; }
    QList<double> bands() const { return m_bands; }
    QString preset() const { return m_preset; }
    Q_INVOKABLE double band(int index) const;

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

    // Attenuation in dB needed so that the combined gain path cannot exceed
    // full scale. Pure function so that AV-003 can be tested without a
    // pipeline. Returns a non-negative number of decibels to remove.
    static double headroomAttenuation(double preampDb, const QList<double> &bandsDb);
    double headroomAttenuation() const;

    // Winamp `.eqf` (F-022). Eleven bytes per preset — ten bands then the
    // preamp — each 0 to 63 with 31 meaning 0 dB, and the scale inverted, so
    // that a lower stored value is a higher gain. SPEC.md §Equaliser.
    static bool decodeEqf(const QByteArray &bytes, QList<double> *bands,
                          double *preamp, QString *name = nullptr);

    static QStringList presetNames();
    static QList<double> presetBands(const QString &name);

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
    bool importEqf(const QString &path);

signals:
    void enabledChanged();
    void preampChanged();
    void bandsChanged();
    void presetChanged();
    void headroomChanged();
    void importFailed(const QString &message);

private:
    void applyGains();
    void configureBands();

    GstElement *m_preampElement = nullptr;
    GstElement *m_filterElement = nullptr;

    bool m_enabled = false;
    double m_preamp = 0.0;
    QList<double> m_bands;
    QString m_preset = QStringLiteral("flat");
    double m_appliedAttenuation = 0.0;
};

} // namespace ferrolux::core
