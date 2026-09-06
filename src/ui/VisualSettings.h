// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// ui/VisualSettings.h — the shape of the displays, as opposed to their colour.
//
// The meter shaders were written with their proportions as literals: nine ranks
// of flame, a bar gap of 0.18, twenty-eight ladder segments. Each was chosen by
// eye and each is a preference rather than a fact, so they belong to the user
// and not to the source.
//
// **Not tokens, and the distinction is the point.** A token set is what the
// panel is *made of* — a finish, per SPEC.md §Design tokens — and every set has
// to carry every token, which is why `tests/tokens_test` holds them all to the
// same vocabulary. These are what the user has *set the displays to*, and they
// survive a change of finish because a preference for a coarse ladder is not a
// preference about paint. Keeping them apart also keeps a finish from being
// able to redefine them, which would make a theme swap change the meaning of a
// setting the user chose.
//
// Every value is clamped on the way in rather than trusted. They arrive from a
// settings file that can be edited by hand and from sliders whose ranges could
// drift, and a flame with zero ranks or a ladder with zero segments is a
// division by zero in a shader — which does not throw, it draws something wrong
// at sixty frames a second.

#pragma once

#include <QObject>

namespace ferrolux::ui {

class VisualSettings : public QObject
{
    Q_OBJECT

    // Spectrum bars, upright and mirrored.
    Q_PROPERTY(double spectrumGap READ spectrumGap WRITE setSpectrumGap NOTIFY changed)
    Q_PROPERTY(double spectrumCap READ spectrumCap WRITE setSpectrumCap NOTIFY changed)

    // Flame. `ranks` is how many receding silhouettes are drawn and is the one
    // with a cost attached: each rank is up to five texture taps per pixel, and
    // BUG-016 is what happened when that went unmeasured at 2160p. The upper
    // bound here matches the shader's own `kMaxRanks`, above which it silently
    // stops drawing them.
    Q_PROPERTY(int flameRanks READ flameRanks WRITE setFlameRanks NOTIFY changed)
    Q_PROPERTY(double flameFront READ flameFront WRITE setFlameFront NOTIFY changed)
    Q_PROPERTY(double flameBack READ flameBack WRITE setFlameBack NOTIFY changed)
    Q_PROPERTY(double flameParallax READ flameParallax WRITE setFlameParallax NOTIFY changed)
    Q_PROPERTY(double flameSoftness READ flameSoftness WRITE setFlameSoftness NOTIFY changed)

    // LED peak ladder.
    Q_PROPERTY(int ladderSegments READ ladderSegments WRITE setLadderSegments NOTIFY changed)
    Q_PROPERTY(double ladderOver READ ladderOver WRITE setLadderOver NOTIFY changed)

public:
    // The values the shaders carried as literals, which are therefore what the
    // displays have always looked like. SPEC.md §Meters records them.
    static constexpr double kSpectrumGap = 0.18;
    static constexpr double kSpectrumCap = 0.035;
    static constexpr int kFlameRanks = 9;
    static constexpr double kFlameFront = 0.52;
    static constexpr double kFlameBack = 1.15;
    static constexpr double kFlameParallax = 2.2;
    static constexpr double kFlameSoftness = 0.9;
    static constexpr int kLadderSegments = 28;
    static constexpr double kLadderOver = 0.8;

    // Ranges. Wider than anyone sensibly wants at either end, because a control
    // that cannot reach an ugly setting cannot be explored, and narrower than
    // the shader's failure modes, because a control that can reach one is a bug
    // with a slider attached.
    static constexpr int kMinFlameRanks = 1;
    static constexpr int kMaxFlameRanks = 16;   // the shader's own kMaxRanks
    static constexpr int kMinLadderSegments = 4;
    static constexpr int kMaxLadderSegments = 64;

    explicit VisualSettings(QObject *parent = nullptr);

    double spectrumGap() const { return m_spectrumGap; }
    double spectrumCap() const { return m_spectrumCap; }
    int flameRanks() const { return m_flameRanks; }
    double flameFront() const { return m_flameFront; }
    double flameBack() const { return m_flameBack; }
    double flameParallax() const { return m_flameParallax; }
    double flameSoftness() const { return m_flameSoftness; }
    int ladderSegments() const { return m_ladderSegments; }
    double ladderOver() const { return m_ladderOver; }

    void setSpectrumGap(double value);
    void setSpectrumCap(double value);
    void setFlameRanks(int value);
    void setFlameFront(double value);
    void setFlameBack(double value);
    void setFlameParallax(double value);
    void setFlameSoftness(double value);
    void setLadderSegments(int value);
    void setLadderOver(double value);

    // Back to the values the displays shipped with. Reachable from the panel,
    // because a set of nine sliders needs a way out of whatever it has been
    // dragged into.
    Q_INVOKABLE void reset();

    // SPEC.md §Settings owns the keys.
    void load();
    void save() const;

signals:
    // One signal for all of them. They are read by shader properties that are
    // re-evaluated together anyway, and nine separate notifications would be
    // nine chances to bind to the wrong one.
    void changed();

private:
    double m_spectrumGap = kSpectrumGap;
    double m_spectrumCap = kSpectrumCap;
    int m_flameRanks = kFlameRanks;
    double m_flameFront = kFlameFront;
    double m_flameBack = kFlameBack;
    double m_flameParallax = kFlameParallax;
    double m_flameSoftness = kFlameSoftness;
    int m_ladderSegments = kLadderSegments;
    double m_ladderOver = kLadderOver;
};

} // namespace ferrolux::ui
