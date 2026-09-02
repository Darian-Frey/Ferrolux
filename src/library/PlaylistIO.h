// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// library/PlaylistIO.h — M3U, M3U8 and PLS serialisation.
//
// Delivers F-013. Formats and their exact conventions are specified in
// SPEC.md §Playlist file formats; the rule this file exists to honour is
// "read tolerantly, write strictly". Unknown directives and malformed lines
// are skipped rather than treated as errors, because a playlist that half
// loads is more useful than one that refuses to.

#pragma once

#include <QList>
#include <QString>

#include "library/PlaylistEntry.h"

namespace ferrolux::library::PlaylistIO {

enum class Format { M3U, M3U8, PLS };

// Chosen from the file's suffix. Anything unrecognised is treated as M3U,
// which is the format most likely to be readable by accident.
Format formatForPath(const QString &path);

// Returns the entries a file describes. Rows come back with Pending metadata:
// any title carried by the playlist is used as a placeholder, and the real
// tags are read afterwards by MetadataReader.
QList<PlaylistEntry> load(const QString &path, QString *error = nullptr);

bool save(const QString &path, const QList<PlaylistEntry> &entries, QString *error = nullptr);

} // namespace ferrolux::library::PlaylistIO
