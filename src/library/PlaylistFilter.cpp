// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "library/PlaylistFilter.h"

#include "library/PlaylistModel.h"

namespace ferrolux::library {

PlaylistFilter::PlaylistFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &PlaylistFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PlaylistFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &PlaylistFilter::countChanged);
}

void PlaylistFilter::setFilterText(const QString &text)
{
    if (m_text == text)
        return;

    m_text = text;
    // Every whitespace-separated term must match somewhere in the row, so
    // "kraft radio" finds "Kraftwerk — Radioactivity". A single substring match
    // would not, and that is the search people actually type.
    m_terms = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    invalidateFilter();
    emit filterTextChanged();
    emit countChanged();
}

bool PlaylistFilter::filterAcceptsRow(int sourceRow, const QModelIndex &parent) const
{
    if (m_terms.isEmpty())
        return true;

    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return true;

    const QModelIndex index = model->index(sourceRow, 0, parent);
    const QString haystack =
        model->data(index, PlaylistModel::TitleRole).toString() + QLatin1Char('\n') +
        model->data(index, PlaylistModel::ArtistRole).toString() + QLatin1Char('\n') +
        model->data(index, PlaylistModel::AlbumRole).toString() + QLatin1Char('\n') +
        model->data(index, PlaylistModel::UrlRole).toUrl().fileName();

    for (const QString &term : m_terms) {
        if (!haystack.contains(term, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

int PlaylistFilter::toSourceRow(int proxyRow) const
{
    const QModelIndex source = mapToSource(index(proxyRow, 0));
    return source.isValid() ? source.row() : -1;
}

int PlaylistFilter::fromSourceRow(int sourceRow) const
{
    if (!sourceModel())
        return -1;
    const QModelIndex proxy = mapFromSource(sourceModel()->index(sourceRow, 0));
    return proxy.isValid() ? proxy.row() : -1;
}

} // namespace ferrolux::library
