// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// core/StreamTimer.h — detection for AV-001, the half that runs.
//
// ARCHITECTURE.md §Key invariants item 1 says the streaming thread does no
// application work. That is a rule about code, and `spec_test` reads the source
// to hold it — but a rule read from the source only covers what the source
// says. It cannot see how long a call actually takes, and the constructs it
// forbids are not the only way to be slow: a mutex the main thread happens to
// be holding, a page fault, a `g_object_set` that turns out to synchronise with
// the pipeline. Each of those is a stall of unbounded length written in code
// that looks instantaneous.
//
// So this measures. It times the application's own streaming-thread callbacks
// from inside them and keeps four numbers, which is what makes the claim
// falsifiable rather than merely argued.
//
// The measurement is itself work on the streaming thread, which is the obvious
// objection. It costs two `steady_clock` reads — a vDSO read, tens of
// nanoseconds, no syscall — and three relaxed atomic updates on a cache line
// nothing else touches while playing. That is two to three orders of magnitude
// below the mutex acquisition it wraps, and it is always on rather than behind
// a build flag: a measurement compiled out of the shipping binary describes a
// program nobody runs. AV-002's `FrameTimer` is always attached for the same
// reason.
//
// What counts as too long. The audio ring buffer is handed to the device in
// segments — `pulsesink` defaults to a 10 ms latency time — so a callback that
// runs for a whole segment has spent the margin the sink had. The budget here
// is 1 ms, an order of magnitude under that: not the point where audio breaks,
// but the point where the code has stopped being obviously safe and somebody
// should look. Underruns are counted separately and by the sink itself, in
// `tools/stress-audio.sh`, because the ear is downstream of everything and only
// the device can say what it actually missed.

#pragma once

#include <QString>

#include <atomic>
#include <chrono>

namespace ferrolux::core {

class StreamTimer
{
public:
    // An order of magnitude under `pulsesink`'s default 10 ms segment. See the
    // header comment: this is a threshold for attention, not for failure.
    static constexpr double kBudgetUs = 1000.0;

    // Called from a streaming thread. Everything else on this class is for the
    // main thread to read afterwards.
    void record(qint64 nanoseconds) noexcept
    {
        m_calls.fetch_add(1, std::memory_order_relaxed);
        m_totalNs.fetch_add(nanoseconds, std::memory_order_relaxed);
        if (double(nanoseconds) / 1000.0 > kBudgetUs)
            m_overBudget.fetch_add(1, std::memory_order_relaxed);

        // Compare-exchange rather than a plain store: two streaming threads can
        // in principle be in different callbacks at once, and a lost maximum is
        // the one sample that mattered.
        qint64 worst = m_worstNs.load(std::memory_order_relaxed);
        while (nanoseconds > worst
               && !m_worstNs.compare_exchange_weak(worst, nanoseconds,
                                                   std::memory_order_relaxed))
            ;
    }

    qint64 calls() const { return m_calls.load(std::memory_order_relaxed); }
    qint64 overBudget() const { return m_overBudget.load(std::memory_order_relaxed); }
    double worstUs() const { return double(m_worstNs.load(std::memory_order_relaxed)) / 1000.0; }
    double meanUs() const
    {
        const qint64 n = calls();
        if (n <= 0)
            return 0.0;
        return double(m_totalNs.load(std::memory_order_relaxed)) / double(n) / 1000.0;
    }

    void reset()
    {
        m_calls.store(0, std::memory_order_relaxed);
        m_totalNs.store(0, std::memory_order_relaxed);
        m_worstNs.store(0, std::memory_order_relaxed);
        m_overBudget.store(0, std::memory_order_relaxed);
    }

    // One line, in the shape `tools/stress-audio.sh` parses. It reports the
    // call count as well as the times, because zero calls and zero microseconds
    // read identically in a summary and mean opposite things — the second is a
    // callback that was never exercised, which is a harness that proved nothing.
    QString summary() const;

private:
    std::atomic<qint64> m_calls{0};
    std::atomic<qint64> m_totalNs{0};
    std::atomic<qint64> m_worstNs{0};
    std::atomic<qint64> m_overBudget{0};
};

// Times one streaming-thread callback. Declared at the top of the callback, so
// the measurement covers everything the callback does including its returns.
class StreamScope
{
public:
    explicit StreamScope(StreamTimer &timer) noexcept
        : m_timer(timer)
        , m_start(std::chrono::steady_clock::now())
    {
    }

    ~StreamScope()
    {
        const auto elapsed = std::chrono::steady_clock::now() - m_start;
        m_timer.record(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

    StreamScope(const StreamScope &) = delete;
    StreamScope &operator=(const StreamScope &) = delete;

private:
    StreamTimer &m_timer;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace ferrolux::core
