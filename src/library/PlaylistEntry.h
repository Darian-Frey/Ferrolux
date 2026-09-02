// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// library/PlaylistEntry.h — one row of the playlist.
//
// Deliberately a plain value type with no behaviour. Metadata arrives
// asynchronously from a worker thread (F-010, AV-008), so a row exists and is
// displayable from the moment it is added, carrying a filename-derived title
// until the real tags land.

#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

namespace ferrolux::library {

// Lifecycle of a row's metadata. Rows are added Pending and move exactly once.
enum class MetadataState {
    Pending,  // queued or in flight
    Loaded,   // tags read, however sparse
    Failed,   // file present but unreadable as audio — see AV-009
    Missing,  // path does not resolve — see AV-010
};

struct PlaylistEntry
{
    QUrl url;
    QString title;
    QString artist;
    QString album;
    qint64 durationNs = -1;
    QDateTime fileDate;
    MetadataState metadata = MetadataState::Pending;

    // What the playlist shows. Falls back to the file's base name so that a row
    // is never blank while its tags are still being read, and so that a file
    // with no useful tags at all still reads sensibly.
    QString displayTitle() const
    {
        if (!title.isEmpty())
            return title;
        const QString path = url.fileName();
        const int dot = path.lastIndexOf('.');
        return dot > 0 ? path.left(dot) : path;
    }
};

} // namespace ferrolux::library
