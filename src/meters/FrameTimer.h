// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// meters/FrameTimer.h — detection for AV-002.
//
// The meters redraw every frame at whatever size the panel occupies, and a
// shader that is cheap at 1080p may not be at 2160p. The failure is a dropped
// frame rather than an error, so nothing reports it: the display simply becomes
// less smooth than it was, gradually, as features are added.
//
// It times the whole window rather than the meters alone, deliberately. The
// meters have no frame of their own — they share one with the playlist, the
// chrome and every binding that runs per frame — and AV-002 names exactly those
// as the compounding risks: independent position pollers, per-frame QML
// bindings that allocate, texture uploads scheduled outside the render pass.
// Timing the meters in isolation would miss all three.
//
// Two quantities, measuring different things:
//
//   interval   wall time between successive frames. Answers "are we holding
//              60 fps", but saturates at the refresh rate — with vsync on, a
//              display with vast headroom and one with none both read 16.7 ms.
//   render     CPU time inside the render pass. Does not saturate, so it is
//              what headroom is computed from, but it excludes GPU execution,
//              which the RHI submits asynchronously.
//
// Neither alone is sufficient, which is why both are kept. To measure headroom
// honestly the swap interval must be disabled so the interval stops saturating
// and becomes the true cost of a frame; `tools/measure-frames.sh` does that.

#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <atomic>

class QQuickWindow;

namespace ferrolux::meters {

class FrameTimer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double meanIntervalMs READ meanIntervalMs NOTIFY sampled)
    Q_PROPERTY(double worstIntervalMs READ worstIntervalMs NOTIFY sampled)
    Q_PROPERTY(double meanRenderMs READ meanRenderMs NOTIFY sampled)
    Q_PROPERTY(double worstRenderMs READ worstRenderMs NOTIFY sampled)
    Q_PROPERTY(double cpuHeadroom READ cpuHeadroom NOTIFY sampled)
    Q_PROPERTY(int frames READ frames NOTIFY sampled)
    Q_PROPERTY(int lateFrames READ lateFrames NOTIFY sampled)

public:
    // 60 fps. SPEC.md sets the meter interval to 16 ms for the same reason.
    static constexpr double kBudgetMs = 1000.0 / 60.0;

    explicit FrameTimer(QObject *parent = nullptr);

    // Connections are direct, so the handlers run on the render thread that
    // emits them rather than being queued back to the GUI thread — queuing
    // would measure the queue rather than the frame.
    void attach(QQuickWindow *window);

    int frames() const { return int(m_frames.load(std::memory_order_relaxed)); }
    int lateFrames() const { return int(m_late.load(std::memory_order_relaxed)); }
    double meanIntervalMs() const;
    double worstIntervalMs() const;
    double meanRenderMs() const;
    double worstRenderMs() const;

    // Fraction of the frame budget left unused by the CPU render pass.
    //
    // A lower bound on the true headroom, not the whole of it. GPU execution is
    // submitted asynchronously by the RHI and is not included, so a shader that
    // costs the GPU dearly can still report a small CPU cost. Read it as "the
    // CPU is not the problem" rather than as "there is room".
    //
    // Wall-clock intervals cannot supply the rest. The compositor paces frames
    // whatever the swap interval is set to, so with vsync nominally disabled
    // the interval does not become the cost of a frame — it becomes erratic.
    // What the interval *can* answer is whether 60 fps is being held, which is
    // the acceptance criterion itself; late frames answer the same question
    // from the other side.
    double cpuHeadroom() const;

    // Fraction of the frame budget left unused by the *whole* frame.
    //
    // Only meaningful where nothing paces the loop. In the application the
    // compositor supplies a frame every 16.7 ms whatever the frame cost, so
    // this reads as zero headroom on a display with plenty and would be a lie.
    // Rendering through QQuickRenderControl with a fence after each frame gives
    // it a true value, which is what tests/frame_bench exists to do; use
    // cpuHeadroom() in the application and this offscreen.
    double frameHeadroom() const;

    Q_INVOKABLE void reset();

    // One line, for a report on exit or a measurement run.
    QString summary() const;

signals:
    void sampled();

private:
    void onBeforeRendering();
    void onAfterRendering();

    // Written on the render thread, read on the GUI thread. Relaxed ordering is
    // enough: these are counters for a diagnostic, and a reader that catches a
    // half-updated pair gets one slightly wrong average, not a wrong decision.
    std::atomic<qint64> m_frames { 0 };
    std::atomic<qint64> m_late { 0 };
    std::atomic<qint64> m_intervalSumNs { 0 };
    std::atomic<qint64> m_intervalWorstNs { 0 };
    std::atomic<qint64> m_renderSumNs { 0 };
    std::atomic<qint64> m_renderWorstNs { 0 };

    QElapsedTimer m_sinceStart;
    qint64 m_lastFrameNs = 0;
    qint64 m_renderBeganNs = 0;
};

} // namespace ferrolux::meters
