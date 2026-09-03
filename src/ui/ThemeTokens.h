// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// ui/ThemeTokens.h — the panel's appearance, loaded from data.
//
// SPEC.md §Design tokens defines the panel as a named token set resolved at
// load, and F-044 requires an alternative finish to be a token set over the
// same geometry rather than an asset pack. Both of those are claims about where
// the values live: in a file that a variant can replace, not scattered through
// the components that draw with them.
//
// So this loads one JSON set and exposes it. It holds no defaults of its own
// beyond refusing to start without a set, and it knows nothing about what any
// token means — a component asks for `readout` and gets a colour, and the
// question of which amber that is belongs to the file.
//
// Three groups, because they answer to different authorities:
//
//   palette   colours. SPEC.md §Design tokens is authoritative for the `ferric`
//             values and tests/tokens_test asserts the file still agrees with it.
//   metrics   geometry in device-independent units, at a reference scale. Never
//             pixels: AV-005 is the whole reason the project exists, and a
//             hairline given in pixels is that defect in miniature (F-040).
//   type      face names and the type scale. The faces are fixed for the project
//             by D-012 and are not a variant's to change; their sizes are.
//
// Scaling is not here. This reports the reference geometry, and qml/Tokens.qml
// multiplies it by the window's scale factor, so there is one place that knows
// how the panel grows and it is next to the components that grow with it.

#pragma once

#include <QColor>
#include <QObject>
#include <QVariantMap>

namespace ferrolux::ui {

class ThemeTokens : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QVariantMap palette READ palette CONSTANT)
    Q_PROPERTY(QVariantMap metrics READ metrics CONSTANT)
    Q_PROPERTY(QVariantMap type READ type CONSTANT)

public:
    explicit ThemeTokens(QObject *parent = nullptr);

    // Returns false and leaves the set untouched if the file is missing, is not
    // an object, or lacks any of the three groups. A half-loaded token set would
    // draw a panel in whatever a missing colour resolves to, which is black, and
    // black chrome on a black readout is not a visible failure.
    bool load(const QString &path);

    QString name() const { return m_name; }
    QVariantMap palette() const { return m_palette; }
    QVariantMap metrics() const { return m_metrics; }
    QVariantMap type() const { return m_type; }

    QString lastError() const { return m_error; }

    // Named lookups. A name that is not in the set is a fault in the set rather
    // than a condition to render around, so each of these warns rather than
    // failing quietly — a token silently resolving to a default is how a variant
    // ends up half-applied and nobody notices which half.
    Q_INVOKABLE QColor colour(const QString &token) const;
    Q_INVOKABLE double metric(const QString &token) const;
    Q_INVOKABLE QString face(const QString &token) const;
    Q_INVOKABLE double size(const QString &token) const;

private:
    QString m_name;
    QString m_error;
    QVariantMap m_palette;
    QVariantMap m_metrics;
    QVariantMap m_type;
};

} // namespace ferrolux::ui
