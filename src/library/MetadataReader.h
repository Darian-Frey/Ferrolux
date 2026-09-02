// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// library/MetadataReader.h — tag extraction on a worker pool.
//
// Delivers the asynchronous half of F-010. Reading tags for twenty thousand
// files takes far longer than a frame, so it never happens on the main thread:
// rows appear immediately showing a filename-derived title, and real metadata
// arrives later in batches (AV-008).
//
// Threading
// ---------
// Work runs on a private QThreadPool, not the global one, so a long import
// cannot starve anything else that later decides to use QtConcurrent. Results
// return to the owner thread by queued invocation; nothing on a worker touches
// the model. The pool is capped rather than set to the core count because tag
// reading is dominated by seeks, and more threads than that turns a spinning
// disk into a thrash.
//
// A file on an unresponsive mount blocks the worker holding it, not the
// interface (AV-010). That is the bound this design actually offers: the UI
// stays live, and one pool thread is lost until the kernel gives up.

#pragma once

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QThreadPool>
#include <QUrl>

#include <memory>

#include "library/PlaylistEntry.h"

namespace ferrolux::library {

class MetadataReader : public QObject
{
    Q_OBJECT

public:
    explicit MetadataReader(QObject *parent = nullptr);
    ~MetadataReader() override;

    // Number of URLs handed to a single worker. Large enough that the queued
    // invocation overhead disappears, small enough that rows keep populating
    // visibly rather than in one late lump.
    static constexpr int kBatchSize = 64;

public slots:
    void enqueue(const QList<QUrl> &urls);

    // Abandons queued work. Batches already running finish and are discarded.
    void cancel();

signals:
    void batchReady(const QList<ferrolux::library::PlaylistEntry> &results);
    void idle();

private:
    QThreadPool m_pool;
    std::shared_ptr<QAtomicInt> m_generation;
    int m_outstanding = 0;
};

} // namespace ferrolux::library
