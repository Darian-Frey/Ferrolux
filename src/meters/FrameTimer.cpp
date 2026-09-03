// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "meters/FrameTimer.h"

#include <QQuickWindow>
#include <QStringList>

#include <algorithm>

namespace ferrolux::meters {
namespace {

double toMs(qint64 nanoseconds)
{
    return double(nanoseconds) / 1'000'000.0;
}

} // namespace

FrameTimer::FrameTimer(QObject *parent)
    : QObject(parent)
{
    m_sinceStart.start();
}

void FrameTimer::attach(QQuickWindow *window)
{
    if (!window)
        return;

    connect(window, &QQuickWindow::beforeRendering,
            this, &FrameTimer::onBeforeRendering, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering,
            this, &FrameTimer::onAfterRendering, Qt::DirectConnection);
}

void FrameTimer::onBeforeRendering()
{
    const qint64 now = m_sinceStart.nsecsElapsed();

    if (m_lastFrameNs > 0) {
        const qint64 interval = now - m_lastFrameNs;
        m_intervalSumNs.fetch_add(interval, std::memory_order_relaxed);

        qint64 worst = m_intervalWorstNs.load(std::memory_order_relaxed);
        while (interval > worst
               && !m_intervalWorstNs.compare_exchange_weak(worst, interval,
                                                           std::memory_order_relaxed)) {
        }

        // Late by a whole frame's grace rather than by a hair: a frame that
        // takes 16.8 ms has not dropped anything, whereas one that takes 33 ms
        // has missed a refresh and is visible as a stutter.
        if (toMs(interval) > kBudgetMs * 1.5)
            m_late.fetch_add(1, std::memory_order_relaxed);

        m_frames.fetch_add(1, std::memory_order_relaxed);
    }

    m_lastFrameNs = now;
    m_renderBeganNs = now;
}

void FrameTimer::onAfterRendering()
{
    if (m_renderBeganNs <= 0)
        return;

    const qint64 spent = m_sinceStart.nsecsElapsed() - m_renderBeganNs;
    m_renderSumNs.fetch_add(spent, std::memory_order_relaxed);

    qint64 worst = m_renderWorstNs.load(std::memory_order_relaxed);
    while (spent > worst
           && !m_renderWorstNs.compare_exchange_weak(worst, spent,
                                                     std::memory_order_relaxed)) {
    }
}

double FrameTimer::meanIntervalMs() const
{
    const qint64 count = m_frames.load(std::memory_order_relaxed);
    return count > 0 ? toMs(m_intervalSumNs.load(std::memory_order_relaxed)) / double(count) : 0.0;
}

double FrameTimer::worstIntervalMs() const
{
    return toMs(m_intervalWorstNs.load(std::memory_order_relaxed));
}

double FrameTimer::meanRenderMs() const
{
    const qint64 count = m_frames.load(std::memory_order_relaxed);
    return count > 0 ? toMs(m_renderSumNs.load(std::memory_order_relaxed)) / double(count) : 0.0;
}

double FrameTimer::worstRenderMs() const
{
    return toMs(m_renderWorstNs.load(std::memory_order_relaxed));
}

double FrameTimer::cpuHeadroom() const
{
    const double mean = meanRenderMs();
    if (mean <= 0.0)
        return 1.0;
    return std::clamp(1.0 - mean / kBudgetMs, 0.0, 1.0);
}

double FrameTimer::frameHeadroom() const
{
    const double mean = meanIntervalMs();
    if (mean <= 0.0)
        return 1.0;
    return std::clamp(1.0 - mean / kBudgetMs, 0.0, 1.0);
}

void FrameTimer::reset()
{
    m_frames.store(0, std::memory_order_relaxed);
    m_late.store(0, std::memory_order_relaxed);
    m_intervalSumNs.store(0, std::memory_order_relaxed);
    m_intervalWorstNs.store(0, std::memory_order_relaxed);
    m_renderSumNs.store(0, std::memory_order_relaxed);
    m_renderWorstNs.store(0, std::memory_order_relaxed);
    m_lastFrameNs = 0;
    emit sampled();
}

QString FrameTimer::summary() const
{
    return QStringLiteral(
               "frames=%1 interval_mean=%2ms interval_worst=%3ms "
               "render_mean=%4ms render_worst=%5ms cpu_headroom=%6 late=%7")
        .arg(frames())
        .arg(meanIntervalMs(), 0, 'f', 3)
        .arg(worstIntervalMs(), 0, 'f', 3)
        .arg(meanRenderMs(), 0, 'f', 3)
        .arg(worstRenderMs(), 0, 'f', 3)
        .arg(cpuHeadroom(), 0, 'f', 3)
        .arg(lateFrames());
}

} // namespace ferrolux::meters
