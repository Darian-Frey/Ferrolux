// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "ui/VisualSettings.h"

#include <QSettings>

#include <algorithm>

namespace ferrolux::ui {
namespace {

// Every setter goes through one of these. Clamping in one place means a range
// cannot be enforced in the setter and forgotten in the loader, which is how a
// hand-edited settings file gets to divide by zero inside a shader.
template <typename T>
bool assign(T &field, T value, T lowest, T highest, T epsilon = T(0))
{
    const T clamped = std::clamp(value, lowest, highest);
    if (std::abs(clamped - field) <= epsilon)
        return false;
    field = clamped;
    return true;
}

} // namespace

VisualSettings::VisualSettings(QObject *parent)
    : QObject(parent)
{
}

void VisualSettings::setSpectrumGap(double value)
{
    if (assign(m_spectrumGap, value, 0.0, 0.6, 1e-4))
        emit changed();
}

void VisualSettings::setSpectrumCap(double value)
{
    if (assign(m_spectrumCap, value, 0.0, 0.15, 1e-4))
        emit changed();
}

void VisualSettings::setFlameRanks(int value)
{
    if (assign(m_flameRanks, value, kMinFlameRanks, kMaxFlameRanks))
        emit changed();
}

void VisualSettings::setFlameFront(double value)
{
    if (assign(m_flameFront, value, 0.05, 2.0, 1e-4))
        emit changed();
}

void VisualSettings::setFlameBack(double value)
{
    if (assign(m_flameBack, value, 0.05, 2.0, 1e-4))
        emit changed();
}

void VisualSettings::setFlameParallax(double value)
{
    if (assign(m_flameParallax, value, 0.0, 8.0, 1e-4))
        emit changed();
}

void VisualSettings::setFlameSoftness(double value)
{
    if (assign(m_flameSoftness, value, 0.0, 3.0, 1e-4))
        emit changed();
}

void VisualSettings::setLadderSegments(int value)
{
    if (assign(m_ladderSegments, value, kMinLadderSegments, kMaxLadderSegments))
        emit changed();
}

void VisualSettings::setLadderOver(double value)
{
    // Never zero and never one. At zero every segment is an over-segment and
    // the ladder is one colour; at one none of them ever is, and the warning
    // tier the ladder exists to show becomes unreachable.
    if (assign(m_ladderOver, value, 0.2, 0.98, 1e-4))
        emit changed();
}

void VisualSettings::reset()
{
    m_spectrumGap = kSpectrumGap;
    m_spectrumCap = kSpectrumCap;
    m_flameRanks = kFlameRanks;
    m_flameFront = kFlameFront;
    m_flameBack = kFlameBack;
    m_flameParallax = kFlameParallax;
    m_flameSoftness = kFlameSoftness;
    m_ladderSegments = kLadderSegments;
    m_ladderOver = kLadderOver;
    emit changed();
}

void VisualSettings::load()
{
    const QSettings settings;

    // Through the setters, so a hand-edited file is clamped exactly as a slider
    // is. Reading straight into the members would make the settings file the
    // one route into the program that skips its own validation.
    setSpectrumGap(settings.value(QStringLiteral("meters/spectrum-gap"), kSpectrumGap).toDouble());
    setSpectrumCap(settings.value(QStringLiteral("meters/spectrum-cap"), kSpectrumCap).toDouble());
    setFlameRanks(settings.value(QStringLiteral("meters/flame-ranks"), kFlameRanks).toInt());
    setFlameFront(settings.value(QStringLiteral("meters/flame-front"), kFlameFront).toDouble());
    setFlameBack(settings.value(QStringLiteral("meters/flame-back"), kFlameBack).toDouble());
    setFlameParallax(settings.value(QStringLiteral("meters/flame-parallax"), kFlameParallax).toDouble());
    setFlameSoftness(settings.value(QStringLiteral("meters/flame-softness"), kFlameSoftness).toDouble());
    setLadderSegments(settings.value(QStringLiteral("meters/ladder-segments"), kLadderSegments).toInt());
    setLadderOver(settings.value(QStringLiteral("meters/ladder-over"), kLadderOver).toDouble());
}

void VisualSettings::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("meters/spectrum-gap"), m_spectrumGap);
    settings.setValue(QStringLiteral("meters/spectrum-cap"), m_spectrumCap);
    settings.setValue(QStringLiteral("meters/flame-ranks"), m_flameRanks);
    settings.setValue(QStringLiteral("meters/flame-front"), m_flameFront);
    settings.setValue(QStringLiteral("meters/flame-back"), m_flameBack);
    settings.setValue(QStringLiteral("meters/flame-parallax"), m_flameParallax);
    settings.setValue(QStringLiteral("meters/flame-softness"), m_flameSoftness);
    settings.setValue(QStringLiteral("meters/ladder-segments"), m_ladderSegments);
    settings.setValue(QStringLiteral("meters/ladder-over"), m_ladderOver);
}

} // namespace ferrolux::ui
