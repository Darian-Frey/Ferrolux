// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "library/PlaylistIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>
#include <QRegularExpression>

#include <algorithm>

namespace ferrolux::library::PlaylistIO {
namespace {

constexpr qint64 kNsPerSecond = 1'000'000'000;

// SPEC.md: paths are written relative when the playlist sits at or above the
// media in the tree, absolute otherwise. A relative path that would need to
// climb out of the playlist's directory is worse than an absolute one — it
// breaks the moment either file moves.
QString referenceFor(const QUrl &url, const QDir &base)
{
    if (!url.isLocalFile())
        return url.toString();

    const QString absolute = QFileInfo(url.toLocalFile()).absoluteFilePath();
    const QString relative = base.relativeFilePath(absolute);
    return relative.startsWith(QLatin1String("..")) ? absolute : relative;
}

QUrl resolve(const QString &reference, const QDir &base)
{
    const QUrl candidate(reference);
    if (candidate.isValid() && !candidate.scheme().isEmpty() && !candidate.isLocalFile())
        return candidate; // http://, and anything else we should not mangle

    QString path = candidate.isLocalFile() ? candidate.toLocalFile() : reference;
    path.replace(QLatin1Char('\\'), QLatin1Char('/')); // playlists written on Windows
    if (QDir::isRelativePath(path))
        path = base.absoluteFilePath(path);
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

// Decodes as UTF-8 where that is valid and falls back to Latin-1 where it is
// not. Plain .m3u was historically Latin-1 and files in the wild are a mix, so
// guessing by suffix alone would mangle half of them.
QString decode(const QByteArray &bytes)
{
    auto toUtf8 = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = toUtf8(bytes);
    if (!toUtf8.hasError())
        return text;
    return QString::fromLatin1(bytes);
}

QList<PlaylistEntry> loadM3u(const QByteArray &bytes, const QDir &base)
{
    QList<PlaylistEntry> entries;
    qint64 pendingDuration = -1;
    QString pendingTitle;

    const QStringList lines = decode(bytes).split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('#'))) {
            if (!line.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive))
                continue; // #EXTM3U and every other directive: ignored, not an error

            const QString payload = line.mid(8);
            const int comma = payload.indexOf(QLatin1Char(','));
            const QString seconds = comma >= 0 ? payload.left(comma) : payload;
            bool ok = false;
            const int value = seconds.trimmed().toInt(&ok);
            pendingDuration = (ok && value > 0) ? qint64(value) * kNsPerSecond : -1;
            pendingTitle = comma >= 0 ? payload.mid(comma + 1).trimmed() : QString();
            continue;
        }

        PlaylistEntry entry;
        entry.url = resolve(line, base);
        entry.durationNs = pendingDuration;
        // The EXTINF text is conventionally "Artist - Title" but nothing
        // guarantees it, and splitting on " - " corrupts any title that
        // legitimately contains one. It is kept whole as a placeholder and
        // replaced wholesale once real tags are read.
        entry.title = pendingTitle;
        entries.append(entry);

        pendingDuration = -1;
        pendingTitle.clear();
    }
    return entries;
}

QList<PlaylistEntry> loadPls(const QByteArray &bytes, const QDir &base)
{
    QHash<int, PlaylistEntry> byIndex;

    const QStringList lines = decode(bytes).split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('[')) || line.startsWith(QLatin1Char(';')))
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();

        // Split "File12" into its name and its index. Keys without a trailing
        // number — NumberOfEntries, Version — fall out here and are ignored.
        int digits = 0;
        while (digits < key.size() && key.at(key.size() - 1 - digits).isDigit())
            ++digits;
        if (digits == 0)
            continue;

        const QString name = key.left(key.size() - digits).toLower();
        const int index = key.right(digits).toInt();

        if (name == QLatin1String("file"))
            byIndex[index].url = resolve(value, base);
        else if (name == QLatin1String("title"))
            byIndex[index].title = value;
        else if (name == QLatin1String("length")) {
            bool ok = false;
            const int seconds = value.toInt(&ok);
            byIndex[index].durationNs = (ok && seconds > 0) ? qint64(seconds) * kNsPerSecond : -1;
        }
    }

    QList<int> indices = byIndex.keys();
    std::sort(indices.begin(), indices.end());

    QList<PlaylistEntry> entries;
    entries.reserve(indices.size());
    for (int index : indices) {
        const PlaylistEntry &entry = byIndex.value(index);
        if (!entry.url.isEmpty())
            entries.append(entry);
    }
    return entries;
}

QByteArray writeM3u(const QList<PlaylistEntry> &entries, const QDir &base)
{
    QString text;
    QTextStream out(&text);
    out << "#EXTM3U\n";
    for (const PlaylistEntry &entry : entries) {
        const qint64 seconds = entry.durationNs > 0 ? entry.durationNs / kNsPerSecond : -1;
        QString label = entry.displayTitle();
        if (!entry.artist.isEmpty())
            label = entry.artist + QStringLiteral(" - ") + label;
        out << "#EXTINF:" << seconds << ',' << label << '\n';
        out << referenceFor(entry.url, base) << '\n';
    }
    out.flush();
    // Both M3U and M3U8 are written UTF-8. SPEC.md records that this diverges
    // from plain M3U's Latin-1 origin, and that every modern player does it.
    return text.toUtf8();
}

QByteArray writePls(const QList<PlaylistEntry> &entries, const QDir &base)
{
    QString text;
    QTextStream out(&text);
    out << "[playlist]\n";
    out << "NumberOfEntries=" << entries.size() << '\n';
    for (int i = 0; i < entries.size(); ++i) {
        const PlaylistEntry &entry = entries.at(i);
        const int n = i + 1; // PLS indices are one-based
        out << "File" << n << '=' << referenceFor(entry.url, base) << '\n';
        QString label = entry.displayTitle();
        if (!entry.artist.isEmpty())
            label = entry.artist + QStringLiteral(" - ") + label;
        out << "Title" << n << '=' << label << '\n';
        out << "Length" << n << '=' << (entry.durationNs > 0 ? entry.durationNs / kNsPerSecond : -1) << '\n';
    }
    out << "Version=2\n";
    out.flush();
    return text.toUtf8();
}

} // namespace

Format formatForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pls"))
        return Format::PLS;
    if (suffix == QLatin1String("m3u8"))
        return Format::M3U8;
    return Format::M3U;
}

QList<PlaylistEntry> load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }

    const QByteArray bytes = file.readAll();
    const QDir base = QFileInfo(path).absoluteDir();
    return formatForPath(path) == Format::PLS ? loadPls(bytes, base) : loadM3u(bytes, base);
}

bool save(const QString &path, const QList<PlaylistEntry> &entries, QString *error)
{
    const QDir base = QFileInfo(path).absoluteDir();
    const QByteArray bytes = formatForPath(path) == Format::PLS
        ? writePls(entries, base)
        : writeM3u(entries, base);

    // QSaveFile so that a failed or interrupted write leaves the previous
    // playlist intact rather than truncated.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace ferrolux::library::PlaylistIO
