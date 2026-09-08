// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "meters/RenderThreadGuard.h"

#include <QThread>

namespace ferrolux::meters {
namespace {

// Relaxed throughout. These are counters read once at the end of a run, not
// synchronisation between the threads they count — the render thread's
// increments need not be visible to the GUI thread in any particular order,
// only eventually and in total. An acquire/release pair here would be ordering
// nothing against anything, on the render path, for a number nobody reads until
// the process is quitting.
std::atomic<QThread *> g_gui{nullptr};
std::atomic<qint64> g_uploads{0};
std::atomic<qint64> g_uploadsOnGui{0};
std::atomic<qint64> g_stages{0};
std::atomic<qint64> g_stagesOffGui{0};
std::atomic<bool> g_renderThreadSeen{false};

} // namespace

void RenderThreadGuard::noteGuiThread(QThread *thread)
{
    QThread *expected = nullptr;
    g_gui.compare_exchange_strong(expected, thread, std::memory_order_relaxed);
}

void RenderThreadGuard::noteStage(QThread *thread)
{
    g_stages.fetch_add(1, std::memory_order_relaxed);
    if (g_gui.load(std::memory_order_relaxed) != thread)
        g_stagesOffGui.fetch_add(1, std::memory_order_relaxed);
}

void RenderThreadGuard::noteUpload(QThread *thread)
{
    g_uploads.fetch_add(1, std::memory_order_relaxed);
    if (g_gui.load(std::memory_order_relaxed) == thread)
        g_uploadsOnGui.fetch_add(1, std::memory_order_relaxed);
    else
        g_renderThreadSeen.store(true, std::memory_order_relaxed);
}

qint64 RenderThreadGuard::uploads() { return g_uploads.load(std::memory_order_relaxed); }
qint64 RenderThreadGuard::uploadsOnGuiThread() { return g_uploadsOnGui.load(std::memory_order_relaxed); }
qint64 RenderThreadGuard::stages() { return g_stages.load(std::memory_order_relaxed); }
qint64 RenderThreadGuard::stagesOffGuiThread() { return g_stagesOffGui.load(std::memory_order_relaxed); }
bool RenderThreadGuard::renderThreadSeen() { return g_renderThreadSeen.load(std::memory_order_relaxed); }

void RenderThreadGuard::reset()
{
    g_uploads.store(0, std::memory_order_relaxed);
    g_uploadsOnGui.store(0, std::memory_order_relaxed);
    g_stages.store(0, std::memory_order_relaxed);
    g_stagesOffGui.store(0, std::memory_order_relaxed);
    g_renderThreadSeen.store(false, std::memory_order_relaxed);
}

QString RenderThreadGuard::summary()
{
    return QStringLiteral("uploads=%1 uploads_on_gui=%2 stages=%3 stages_off_gui=%4 threads_distinct=%5")
        .arg(uploads())
        .arg(uploadsOnGuiThread())
        .arg(stages())
        .arg(stagesOffGuiThread())
        .arg(renderThreadSeen() ? QStringLiteral("yes") : QStringLiteral("no"));
}

} // namespace ferrolux::meters
