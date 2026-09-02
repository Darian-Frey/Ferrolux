// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "library/PlaylistModel.h"

#include <QBitArray>
#include <QDirIterator>
#include <QFileInfo>
#include <QRandomGenerator>

#include "library/PlaylistIO.h"

#include <algorithm>

namespace ferrolux::library {
namespace {

// Fisher-Yates over a half-open range, so that a reshuffle on wrap can leave
// already-played entries alone if that is ever wanted.
void shuffleRange(QList<int> &order, int first, int last)
{
    for (int i = last - 1; i > first; --i) {
        const int j = first + int(QRandomGenerator::global()->bounded(i - first + 1));
        order.swapItemsAt(i, j);
    }
}

} // namespace

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const PlaylistEntry &entry = m_entries.at(index.row());
    switch (role) {
    case UrlRole:           return entry.url;
    case TitleRole:         return entry.displayTitle();
    case ArtistRole:        return entry.artist;
    case AlbumRole:         return entry.album;
    case DurationRole:      return entry.durationNs;
    case FileDateRole:      return entry.fileDate;
    case MetadataStateRole: return int(entry.metadata);
    case IsCurrentRole:     return index.row() == currentRow();
    case Qt::DisplayRole:   return entry.displayTitle();
    default:                return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        { UrlRole, "url" },
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { AlbumRole, "album" },
        { DurationRole, "duration" },
        { FileDateRole, "fileDate" },
        { MetadataStateRole, "metadataState" },
        { IsCurrentRole, "isCurrent" },
    };
}

QStringList PlaylistModel::audioSuffixes()
{
    return { QStringLiteral("flac"), QStringLiteral("mp3"),  QStringLiteral("ogg"),
             QStringLiteral("oga"),  QStringLiteral("opus"), QStringLiteral("m4a"),
             QStringLiteral("aac"),  QStringLiteral("wav"),  QStringLiteral("aiff"),
             QStringLiteral("aif"),  QStringLiteral("wv"),   QStringLiteral("mpc"),
             QStringLiteral("alac") };
}

void PlaylistModel::addPaths(const QList<QUrl> &inputs)
{
    static const QStringList suffixes = audioSuffixes();

    QList<QUrl> found;
    for (const QUrl &input : inputs) {
        if (!input.isLocalFile()) {
            found.append(input); // a stream URL: pass it through untouched
            continue;
        }

        const QFileInfo info(input.toLocalFile());
        if (info.isDir()) {
            QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::Readable,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = it.next();
                if (suffixes.contains(QFileInfo(candidate).suffix().toLower()))
                    found.append(QUrl::fromLocalFile(candidate));
            }
        } else if (suffixes.contains(info.suffix().toLower())) {
            found.append(QUrl::fromLocalFile(info.absoluteFilePath()));
        }
    }

    // Sorted, so that adding a folder yields album order rather than whatever
    // order the filesystem happened to return.
    std::sort(found.begin(), found.end(),
              [](const QUrl &a, const QUrl &b) { return a.toString() < b.toString(); });
    addUrls(found);
}

void PlaylistModel::addUrls(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;

    const int first = int(m_entries.size());
    beginInsertRows(QModelIndex(), first, first + int(urls.size()) - 1);

    for (const QUrl &url : urls) {
        PlaylistEntry entry;
        entry.url = url;
        m_entries.append(entry);
    }

    if (!m_shuffle) {
        for (int row = first; row < m_entries.size(); ++row)
            m_order.append(row);
    } else {
        // Insert into the unplayed remainder of the current pass rather than
        // reshuffling, so that adding files mid-listen does not discard the
        // permutation already in progress. F-012 requires the shuffle state to
        // survive track changes; discarding it on every add would defeat that.
        for (int row = first; row < m_entries.size(); ++row) {
            const int lower = m_cursor + 1;
            const int span = int(m_order.size()) - lower + 1;
            const int at = lower + (span > 0 ? int(QRandomGenerator::global()->bounded(span)) : 0);
            m_order.insert(qBound(lower, at, int(m_order.size())), row);
        }
    }

    endInsertRows();

    emit countChanged();
    emit metadataNeeded(urls);
    emitOrderSignals();
}

void PlaylistModel::removeRows(QList<int> rows)
{
    if (rows.isEmpty())
        return;

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.first() < 0 || rows.last() >= m_entries.size())
        return;

    takeSnapshot();

    const int currentBefore = currentRow();
    const bool currentSurvives = !rows.contains(currentBefore);
    const QUrl currentUrl = currentBefore >= 0 ? m_entries.at(currentBefore).url : QUrl();

    // Remove from the back so that the indices in `rows` stay valid as we go.
    for (int i = int(rows.size()) - 1; i >= 0; --i) {
        const int row = rows.at(i);
        beginRemoveRows(QModelIndex(), row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    }

    // Rebuild the order with the surviving indices renumbered. Doing this in one
    // pass rather than patching per removal keeps it O(n) instead of O(n·k).
    const int originalSize = int(m_entries.size()) + int(rows.size());
    QList<int> remap(originalSize, -1);
    for (int old = 0, newIndex = 0, removedAt = 0; old < originalSize; ++old) {
        if (removedAt < rows.size() && rows.at(removedAt) == old) {
            ++removedAt;
            continue;
        }
        remap[old] = newIndex++;
    }

    QList<int> rebuilt;
    rebuilt.reserve(m_entries.size());
    int newCursor = -1;
    for (int position = 0; position < m_order.size(); ++position) {
        const int mapped = remap.value(m_order.at(position), -1);
        if (mapped < 0)
            continue;
        if (currentSurvives && m_order.at(position) == currentBefore)
            newCursor = int(rebuilt.size());
        rebuilt.append(mapped);
    }
    m_order = rebuilt;

    if (!currentSurvives) {
        // The playing entry was removed. The cursor lands where it was, so that
        // a subsequent advance continues from the right place, but no
        // currentEntryChanged is emitted: removing a row from the playlist must
        // not interrupt what is already coming out of the speakers.
        m_cursor = m_order.isEmpty() ? -1 : qBound(0, m_cursor, int(m_order.size()) - 1);
        emit currentRowChanged();
    } else {
        m_cursor = newCursor;
        Q_UNUSED(currentUrl)
    }

    emit countChanged();
    emit canUndoChanged();
    emitOrderSignals();
}

void PlaylistModel::clear()
{
    if (m_entries.isEmpty())
        return;

    takeSnapshot();

    beginResetModel();
    m_entries.clear();
    m_order.clear();
    m_cursor = -1;
    endResetModel();

    emit countChanged();
    emit currentRowChanged();
    emit canUndoChanged();
    emitOrderSignals();
}

bool PlaylistModel::undo()
{
    if (!m_undo.valid)
        return false;

    beginResetModel();
    m_entries = m_undo.entries;
    m_order = m_undo.order;
    m_cursor = m_undo.cursor;
    endResetModel();

    m_undo = Snapshot{};

    emit countChanged();
    emit currentRowChanged();
    emit canUndoChanged();
    emitOrderSignals();
    return true;
}

bool PlaylistModel::moveRows(int from, int count, int destination)
{
    if (count <= 0 || from < 0 || from + count > m_entries.size())
        return false;
    if (destination < 0 || destination > m_entries.size())
        return false;
    if (destination >= from && destination <= from + count)
        return false;

    const int currentBefore = currentRow();

    if (!beginMoveRows(QModelIndex(), from, from + count - 1, QModelIndex(), destination))
        return false;

    QList<PlaylistEntry> moved;
    moved.reserve(count);
    for (int i = 0; i < count; ++i)
        moved.append(m_entries.at(from + i));
    m_entries.remove(from, count);

    const int insertAt = destination > from ? destination - count : destination;
    for (int i = 0; i < count; ++i)
        m_entries.insert(insertAt + i, moved.at(i));

    endMoveRows();

    // Build old-index → new-index and reapply it to the play order.
    QList<int> oldToNew(m_entries.size(), -1);
    for (int i = 0; i < count; ++i)
        oldToNew[from + i] = insertAt + i;
    int cursorNew = 0;
    for (int old = 0; old < m_entries.size(); ++old) {
        if (old >= from && old < from + count)
            continue;
        while (cursorNew >= insertAt && cursorNew < insertAt + count)
            ++cursorNew;
        oldToNew[old] = cursorNew++;
    }

    if (!m_shuffle) {
        rebuildOrder();
        m_cursor = currentBefore >= 0 ? oldToNew.value(currentBefore, -1) : -1;
    } else {
        for (int &index : m_order)
            index = oldToNew.value(index, index);
    }

    emit currentRowChanged();
    emitOrderSignals();
    return true;
}

int PlaylistModel::moveSelection(QList<int> rows, int destination)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty() || rows.first() < 0 || rows.last() >= m_entries.size())
        return -1;
    if (destination < 0 || destination > m_entries.size())
        return -1;

    // Membership is tested once per row across two passes, so asking the sorted
    // list each time makes the whole move quadratic in the selection size. A
    // bitmask makes both passes linear: measured on 20,000 entries moving a
    // 10,000-row selection, 790 ms became a few. See IMP-003.
    QBitArray selected(int(m_entries.size()), false);
    for (int row : std::as_const(rows))
        selected.setBit(row);

    // Where the block lands once the selected rows have been lifted out: the
    // number of unselected rows that sit above the destination.
    int insertAt = 0;
    for (int row = 0; row < destination; ++row) {
        if (!selected.testBit(row))
            ++insertAt;
    }

    // A move that would put the block back exactly where it already is.
    if (rows.size() == rows.last() - rows.first() + 1 && rows.first() == insertAt)
        return -1;

    const int currentBefore = currentRow();

    QList<PlaylistEntry> moved;
    QList<int> movedFrom;
    QList<PlaylistEntry> remaining;
    QList<int> remainingFrom;
    moved.reserve(rows.size());
    remaining.reserve(m_entries.size() - rows.size());

    for (int row = 0; row < m_entries.size(); ++row) {
        if (selected.testBit(row)) {
            moved.append(m_entries.at(row));
            movedFrom.append(row);
        } else {
            remaining.append(m_entries.at(row));
            remainingFrom.append(row);
        }
    }

    QList<PlaylistEntry> rebuilt;
    QList<int> oldToNew(m_entries.size(), -1);
    rebuilt.reserve(m_entries.size());

    for (int i = 0; i < insertAt; ++i) {
        oldToNew[remainingFrom.at(i)] = int(rebuilt.size());
        rebuilt.append(remaining.at(i));
    }
    for (int i = 0; i < moved.size(); ++i) {
        oldToNew[movedFrom.at(i)] = int(rebuilt.size());
        rebuilt.append(moved.at(i));
    }
    for (int i = insertAt; i < remaining.size(); ++i) {
        oldToNew[remainingFrom.at(i)] = int(rebuilt.size());
        rebuilt.append(remaining.at(i));
    }

    // A reset rather than a sequence of beginMoveRows: an arbitrary selection
    // is an arbitrary permutation, and expressing it as legal single-row moves
    // costs more than it saves. The harness therefore commits the move on drop
    // rather than continuously during the drag.
    beginResetModel();
    m_entries = std::move(rebuilt);
    if (!m_shuffle) {
        rebuildOrder();
        m_cursor = currentBefore >= 0 ? oldToNew.value(currentBefore, -1) : -1;
    } else {
        for (int &index : m_order)
            index = oldToNew.value(index, index);
    }
    endResetModel();

    emit currentRowChanged();
    emitOrderSignals();
    return insertAt;
}

void PlaylistModel::sortBy(SortKey key, Qt::SortOrder order)
{
    if (m_entries.size() < 2)
        return;

    const int currentBefore = currentRow();

    QList<int> indices(m_entries.size());
    for (int i = 0; i < indices.size(); ++i)
        indices[i] = i;

    const auto less = [this, key](int a, int b) {
        const PlaylistEntry &x = m_entries.at(a);
        const PlaylistEntry &y = m_entries.at(b);
        switch (key) {
        case ByArtist:   return x.artist.localeAwareCompare(y.artist) < 0;
        case ByAlbum:    return x.album.localeAwareCompare(y.album) < 0;
        case ByDuration: return x.durationNs < y.durationNs;
        case ByPath:     return x.url.toString().localeAwareCompare(y.url.toString()) < 0;
        case ByFileDate: return x.fileDate < y.fileDate;
        case ByTitle:
        default:         return x.displayTitle().localeAwareCompare(y.displayTitle()) < 0;
        }
    };

    // Stable, so that a sort on a key with many equal values preserves whatever
    // ordering the user already had rather than scrambling it.
    std::stable_sort(indices.begin(), indices.end(),
                     [&](int a, int b) { return order == Qt::AscendingOrder ? less(a, b) : less(b, a); });

    QList<PlaylistEntry> sorted;
    sorted.reserve(m_entries.size());
    QList<int> oldToNew(m_entries.size(), -1);
    for (int position = 0; position < indices.size(); ++position) {
        oldToNew[indices.at(position)] = position;
        sorted.append(m_entries.at(indices.at(position)));
    }

    beginResetModel();
    m_entries = std::move(sorted);
    if (!m_shuffle) {
        rebuildOrder();
        m_cursor = currentBefore >= 0 ? oldToNew.value(currentBefore, -1) : -1;
    } else {
        for (int &index : m_order)
            index = oldToNew.value(index, index);
    }
    endResetModel();

    emit currentRowChanged();
    emitOrderSignals();
}

bool PlaylistModel::loadFrom(const QUrl &fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QString error;
    const QList<PlaylistEntry> loaded = PlaylistIO::load(path, &error);
    if (loaded.isEmpty() && !error.isEmpty()) {
        emit ioError(error);
        return false;
    }

    takeSnapshot();

    beginResetModel();
    m_entries = loaded;
    m_cursor = -1;
    rebuildOrder();
    endResetModel();

    QList<QUrl> urls;
    urls.reserve(m_entries.size());
    for (const PlaylistEntry &entry : std::as_const(m_entries))
        urls.append(entry.url);

    emit countChanged();
    emit currentRowChanged();
    emit canUndoChanged();
    // The playlist file's own titles are placeholders; real tags still get read.
    emit metadataNeeded(urls);
    emitOrderSignals();
    return true;
}

bool PlaylistModel::saveTo(const QUrl &fileUrl) const
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QString error;
    if (PlaylistIO::save(path, m_entries, &error))
        return true;
    emit const_cast<PlaylistModel *>(this)->ioError(error);
    return false;
}

void PlaylistModel::applyMetadata(const QList<PlaylistEntry> &results)
{
    if (results.isEmpty())
        return;

    QHash<QUrl, const PlaylistEntry *> byUrl;
    byUrl.reserve(results.size());
    for (const PlaylistEntry &result : results)
        byUrl.insert(result.url, &result);

    // Rows are matched by URL, not index: the playlist may have been sorted or
    // edited while the read was in flight (AV-008).
    int firstChanged = -1;
    int lastChanged = -1;
    for (int row = 0; row < m_entries.size(); ++row) {
        const auto it = byUrl.constFind(m_entries.at(row).url);
        if (it == byUrl.constEnd())
            continue;

        PlaylistEntry &entry = m_entries[row];
        entry.title = (*it)->title;
        entry.artist = (*it)->artist;
        entry.album = (*it)->album;
        entry.durationNs = (*it)->durationNs;
        entry.fileDate = (*it)->fileDate;
        entry.metadata = (*it)->metadata;

        if (firstChanged < 0)
            firstChanged = row;
        lastChanged = row;
    }

    if (firstChanged >= 0)
        emit dataChanged(index(firstChanged), index(lastChanged));
}

void PlaylistModel::setAuthoritativeDuration(const QUrl &url, qint64 durationNs)
{
    if (durationNs <= 0)
        return;

    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).url != url)
            continue;
        if (m_entries.at(row).durationNs == durationNs)
            return;
        m_entries[row].durationNs = durationNs;
        emit dataChanged(index(row), index(row), { DurationRole });
        return;
    }
}

void PlaylistModel::setCurrentRow(int row)
{
    if (row < -1 || row >= m_entries.size())
        return;

    if (row < 0) {
        setCursor(-1);
        return;
    }

    const int position = int(m_order.indexOf(row));
    if (position < 0)
        return;

    setCursor(position);
    emit currentEntryChanged(m_entries.at(row).url);
}

void PlaylistModel::selectWithoutPlaying(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;

    const int position = int(m_order.indexOf(row));
    if (position < 0)
        return;

    setCursor(position);
    emit currentEntryPrepared(m_entries.at(row).url);
}

void PlaylistModel::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle)
        return;

    const int currentBefore = currentRow();
    m_shuffle = shuffle;

    if (!m_shuffle) {
        rebuildOrder();
        m_cursor = currentBefore >= 0 ? currentBefore : -1;
    } else {
        rebuildOrder();
        if (currentBefore >= 0) {
            // Put the playing track at the head of the new permutation so that
            // enabling shuffle does not appear to skip.
            const int at = int(m_order.indexOf(currentBefore));
            if (at > 0)
                m_order.swapItemsAt(0, at);
            m_cursor = 0;
            shuffleRange(m_order, 1, int(m_order.size()));
        } else {
            m_cursor = -1;
        }
    }

    emit shuffleChanged();
    emit currentRowChanged();
    emitOrderSignals();
}

void PlaylistModel::setRepeat(RepeatMode mode)
{
    if (m_repeat == mode)
        return;
    m_repeat = mode;
    emit repeatChanged();
    emitOrderSignals();
}

int PlaylistModel::nextRow() const
{
    if (m_order.isEmpty())
        return -1;
    if (m_repeat == RepeatOne && m_cursor >= 0)
        return m_order.at(m_cursor);
    if (m_cursor < 0)
        return m_order.first();
    if (m_cursor + 1 < m_order.size())
        return m_order.at(m_cursor + 1);
    return m_repeat == RepeatAll ? m_order.first() : -1;
}

int PlaylistModel::previousRow() const
{
    if (m_order.isEmpty())
        return -1;
    if (m_repeat == RepeatOne && m_cursor >= 0)
        return m_order.at(m_cursor);
    if (m_cursor <= 0)
        return m_repeat == RepeatAll ? m_order.last() : -1;
    return m_order.at(m_cursor - 1);
}

bool PlaylistModel::advance()
{
    return step(true);
}

bool PlaylistModel::advanceForHandover()
{
    return step(false);
}

bool PlaylistModel::step(bool notify)
{
    if (m_order.isEmpty())
        return false;

    if (m_repeat == RepeatOne && m_cursor >= 0) {
        if (notify)
            emit currentEntryChanged(m_entries.at(m_order.at(m_cursor)).url);
        return true;
    }

    if (m_cursor + 1 < m_order.size()) {
        setCursor(m_cursor + 1);
    } else if (m_repeat == RepeatAll) {
        // A pass has been exhausted, so a shuffled list earns a fresh
        // permutation. F-012 requires no repeats *until* exhaustion, not never.
        if (m_shuffle)
            shuffleRange(m_order, 0, int(m_order.size()));
        setCursor(0);
    } else {
        return false;
    }

    if (notify)
        emit currentEntryChanged(m_entries.at(m_order.at(m_cursor)).url);
    return true;
}

bool PlaylistModel::retreat()
{
    const int target = previousRow();
    if (target < 0)
        return false;

    const int position = int(m_order.indexOf(target));
    if (position < 0)
        return false;

    setCursor(position);
    emit currentEntryChanged(m_entries.at(target).url);
    return true;
}

void PlaylistModel::rebuildOrder()
{
    m_order.resize(m_entries.size());
    for (int i = 0; i < m_order.size(); ++i)
        m_order[i] = i;
    if (m_shuffle)
        shuffleRange(m_order, 0, int(m_order.size()));
}

void PlaylistModel::takeSnapshot()
{
    m_undo.valid = true;
    m_undo.entries = m_entries;
    m_undo.order = m_order;
    m_undo.cursor = m_cursor;
}

void PlaylistModel::setCursor(int cursor)
{
    if (m_cursor == cursor)
        return;
    m_cursor = cursor;
    emit currentRowChanged();
    emitOrderSignals();
}

void PlaylistModel::emitOrderSignals()
{
    const int next = nextRow();
    emit nextEntryChanged(next >= 0 ? m_entries.at(next).url : QUrl());
}

} // namespace ferrolux::library
