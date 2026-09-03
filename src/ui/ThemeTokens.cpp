// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "ui/ThemeTokens.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace ferrolux::ui {
namespace {

Q_LOGGING_CATEGORY(log, "ferrolux.ui")

} // namespace

ThemeTokens::ThemeTokens(QObject *parent)
    : QObject(parent)
{
}

bool ThemeTokens::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) {
        m_error = QStringLiteral("%1: %2 at offset %3")
                      .arg(path, parse.errorString())
                      .arg(parse.offset);
        return false;
    }
    if (!document.isObject()) {
        m_error = QStringLiteral("%1: not a JSON object").arg(path);
        return false;
    }

    const QJsonObject root = document.object();
    for (const QString &group : { QStringLiteral("palette"), QStringLiteral("metrics"),
                                  QStringLiteral("type") }) {
        if (!root.value(group).isObject()) {
            m_error = QStringLiteral("%1: missing or malformed \"%2\"").arg(path, group);
            return false;
        }
    }

    m_name = root.value(QStringLiteral("name")).toString();
    m_palette = root.value(QStringLiteral("palette")).toObject().toVariantMap();
    m_metrics = root.value(QStringLiteral("metrics")).toObject().toVariantMap();
    m_type = root.value(QStringLiteral("type")).toObject().toVariantMap();
    m_error.clear();
    return true;
}

QColor ThemeTokens::colour(const QString &token) const
{
    const auto found = m_palette.constFind(token);
    if (found == m_palette.constEnd()) {
        qCWarning(log) << "no colour token" << token << "in set" << m_name;
        return {};
    }

    // Six-digit hex throughout. Qt reads an eight-digit colour as #AARRGGBB —
    // alpha first — not the #RRGGBBAA most tools emit, so an alpha appended to a
    // token would silently become a different, translucent colour rather than
    // failing. That is not hypothetical here; it happened once already and made
    // the flame display purple.
    const QColor colour(found.value().toString());
    if (!colour.isValid())
        qCWarning(log) << "colour token" << token << "is not a colour:" << found.value();
    return colour;
}

double ThemeTokens::metric(const QString &token) const
{
    const auto found = m_metrics.constFind(token);
    if (found == m_metrics.constEnd()) {
        qCWarning(log) << "no metric token" << token << "in set" << m_name;
        return 0.0;
    }
    return found.value().toDouble();
}

QString ThemeTokens::face(const QString &token) const
{
    const auto found = m_type.constFind(token);
    if (found == m_type.constEnd()) {
        qCWarning(log) << "no type token" << token << "in set" << m_name;
        return {};
    }
    return found.value().toString();
}

// The type scale lives with the faces rather than with the geometry, because a
// size means nothing without the face it is a size of: 11 in a condensed sans
// legend and 11 in a seven-segment readout are not comparable quantities.
double ThemeTokens::size(const QString &token) const
{
    const auto found = m_type.constFind(token);
    if (found == m_type.constEnd()) {
        qCWarning(log) << "no type size token" << token << "in set" << m_name;
        return 0.0;
    }
    return found.value().toDouble();
}

} // namespace ferrolux::ui
