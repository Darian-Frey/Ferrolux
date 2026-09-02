// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// library/PlaylistFilter.h — the live text filter over the playlist.
//
// Delivers the filtering half of F-014. This proxy never sorts: sorting
// permutes the model because it changes play order, whereas F-014 requires a
// filter to narrow "the visible rows without altering play order". Keeping the
// two mechanisms apart is what makes that guarantee structural rather than
// something to remember.
//
// Matching is over strings already resident in the model, never by re-reading
// tags — the specific mistake AV-008 warns about.

#pragma once

#include <QSortFilterProxyModel>
#include <QStringList>

namespace ferrolux::library {

class PlaylistFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit PlaylistFilter(QObject *parent = nullptr);

    QString filterText() const { return m_text; }
    void setFilterText(const QString &text);

    // Selection and playback are expressed in source rows, since the model owns
    // play order and knows nothing about what happens to be visible.
    Q_INVOKABLE int toSourceRow(int proxyRow) const;
    Q_INVOKABLE int fromSourceRow(int sourceRow) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override;

signals:
    void filterTextChanged();
    void countChanged();

private:
    QString m_text;
    QStringList m_terms;
};

} // namespace ferrolux::library
