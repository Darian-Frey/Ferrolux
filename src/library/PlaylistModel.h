// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// library/PlaylistModel.h — the playlist, and the single owner of play order.
//
// Delivers F-010 through F-012. Per ARCHITECTURE.md §Key invariants item 5,
// this class decides what plays next; Engine is told, and never chooses. Nothing
// outside this class may compute an advance.
//
// Play order
// ----------
// m_order is a permutation of source row indices. In sequential mode it is the
// identity; under shuffle it is a genuine permutation, generated once and then
// walked (F-012). m_cursor is a position *within m_order*, not a row index, so
// that toggling shuffle never loses the current track.
//
// The distinction matters because independent random picks are the naive
// implementation and are observably wrong: they repeat tracks before the list is
// exhausted. Rows added while shuffled are inserted at random positions after
// the cursor, so they join the current pass rather than forcing a reshuffle.
//
// Sorting versus filtering
// ------------------------
// Sorting permutes this model and therefore changes play order. Filtering does
// not, and lives in a proxy — F-014 says a filter "narrows the visible rows
// without altering play order", which implies sorting does alter it. AV-008's
// warning against "sorting that rebuilds the model" is about re-reading tags,
// not about reordering an in-memory vector.

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QUrl>

#include "library/PlaylistEntry.h"

namespace ferrolux::library {

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(RepeatMode repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)

public:
    enum Roles {
        UrlRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        FileDateRole,
        MetadataStateRole,
        IsCurrentRole,
    };
    Q_ENUM(Roles)

    enum RepeatMode { RepeatOff, RepeatAll, RepeatOne };
    Q_ENUM(RepeatMode)

    // Sort keys per F-014. Path and FileDate are included because they are the
    // only stable orderings available before metadata has arrived.
    enum SortKey { ByTitle, ByArtist, ByAlbum, ByDuration, ByPath, ByFileDate };
    Q_ENUM(SortKey)

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentRow() const { return m_cursor < 0 ? -1 : m_order.value(m_cursor, -1); }
    bool shuffle() const { return m_shuffle; }
    RepeatMode repeat() const { return m_repeat; }
    bool canUndo() const { return m_undo.valid; }

    const PlaylistEntry &entryAt(int row) const { return m_entries.at(row); }

    // Applies metadata read on a worker thread. Rows are addressed by URL rather
    // than index because the playlist may have been sorted or edited while the
    // read was in flight.
    void applyMetadata(const QList<PlaylistEntry> &results);

    // Corrects a row's duration from the engine, which demuxes the stream and is
    // therefore authoritative where the tag is only an estimate. A VBR MP3 with
    // no Xing or VBRI header is extrapolated from its first frame and can be
    // out by 20%; see SPEC.md §Duration.
    void setAuthoritativeDuration(const QUrl &url, qint64 durationNs);

public slots:
    // Raw URLs, already known to be playable files. Folders passed here become
    // rows in their own right, which is never what anyone wants — use addPaths.
    void addUrls(const QList<QUrl> &urls);

    // The route everything user-facing should take: expands directories
    // recursively, keeps only things that look like audio, and sorts the
    // result. F-010 asks for files, whole folders and a drag-and-drop
    // selection, and all three arrive here.
    void addPaths(const QList<QUrl> &inputs);

    // Suffixes treated as audio. Matching by suffix rather than by content:
    // sniffing twenty thousand files to decide whether to list them would
    // defeat the point of listing them.
    static QStringList audioSuffixes();
    void removeRows(QList<int> rows);
    void clear();
    bool undo();

    // Drag reorder (F-011). Moves count rows starting at from, to land before
    // destination in the pre-move indexing.
    bool moveRows(int from, int count, int destination);

    // Multi-row drag (F-011). Moves an arbitrary, possibly non-contiguous
    // selection so that it becomes one contiguous block landing before
    // `destination` in the pre-move indexing, preserving the selected rows'
    // relative order. Returns the new index of the first moved row, or -1 if
    // the move was rejected — callers use it to follow the selection, which
    // has different row numbers afterwards.
    //
    // Collapsing a scattered selection into a block is what every file manager
    // and playlist does; the alternative, preserving the gaps between selected
    // rows, has no sensible meaning once the rows between them have moved.
    int moveSelection(QList<int> rows, int destination);

    void sortBy(SortKey key, Qt::SortOrder order = Qt::AscendingOrder);

    // Playlist file I/O (F-013). Loading replaces the contents and is undoable,
    // because replacing a playlist is as destructive as clearing one. Format is
    // chosen from the file's suffix; see SPEC.md §Playlist file formats.
    bool loadFrom(const QUrl &fileUrl);
    bool saveTo(const QUrl &fileUrl) const;

    void setCurrentRow(int row);
    void setShuffle(bool shuffle);
    void setRepeat(RepeatMode mode);

    // Advance queries. Return -1 when there is nowhere to go, which is how
    // repeat-off signals the end of the list rather than by wrapping silently.
    int nextRow() const;
    int previousRow() const;
    bool advance();
    bool retreat();

    // Moves the cursor to reflect a handover that playback has *already* made,
    // without emitting currentEntryChanged. Emitting it would tell the engine to
    // load the track it is in the middle of playing, which would produce exactly
    // the gap gapless exists to avoid. See F-005 and AV-006.
    bool advanceForHandover();

signals:
    void countChanged();
    void currentRowChanged();
    void shuffleChanged();
    void repeatChanged();
    void canUndoChanged();

    // Emitted whenever the current entry changes for any reason — advance,
    // explicit selection, or the current row being removed. Engine listens to
    // this; it is the only route by which anything starts playing.
    void currentEntryChanged(const QUrl &url);

    // The next URL in play order, or an empty QUrl when the list ends here.
    // Engine caches this for the gapless handover, which runs on a streaming
    // thread and must not query this model. See AV-001 and AV-006.
    void nextEntryChanged(const QUrl &url);

    void metadataNeeded(const QList<QUrl> &urls);
    void ioError(const QString &message);

private:
    struct Snapshot {
        bool valid = false;
        QList<PlaylistEntry> entries;
        QList<int> order;
        int cursor = -1;
    };

    bool step(bool notify);
    void rebuildOrder();
    void reshuffleFrom(int cursor);
    void takeSnapshot();
    void emitOrderSignals();
    void setCursor(int cursor);

    QList<PlaylistEntry> m_entries;
    QList<int> m_order;
    int m_cursor = -1;
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatOff;
    Snapshot m_undo;
};

} // namespace ferrolux::library
