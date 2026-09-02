// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "library/MetadataReader.h"

#include <QFileInfo>
#include <QRunnable>
#include <QThread>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
#include <taglib/tpropertymap.h>

namespace ferrolux::library {
namespace {

// TagLib hands back its own string type with a declared encoding that malformed
// files routinely lie about. Asking for UTF-8 explicitly is what keeps ID3v2
// frames with disagreeing encodings from arriving as mojibake — see AV-009.
QString toQString(const TagLib::String &value)
{
    return QString::fromStdString(value.to8Bit(true)).trimmed();
}

PlaylistEntry readOne(const QUrl &url)
{
    PlaylistEntry entry;
    entry.url = url;

    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isReadable()) {
        entry.metadata = MetadataState::Missing;
        return entry;
    }

    entry.fileDate = info.lastModified();

    TagLib::FileRef file(QFile::encodeName(path).constData(), true,
                         TagLib::AudioProperties::Average);
    if (file.isNull()) {
        entry.metadata = MetadataState::Failed;
        return entry;
    }

    if (const TagLib::Tag *tag = file.tag()) {
        entry.title = toQString(tag->title());
        entry.artist = toQString(tag->artist());
        entry.album = toQString(tag->album());
    }

    if (const TagLib::AudioProperties *properties = file.audioProperties()) {
        const int milliseconds = properties->lengthInMilliseconds();
        // A declared duration of zero is not a fact, it is a missing field.
        // Treating it as one makes seeking wrong, which is the AV-009 failure
        // that matters rather than the cosmetic one.
        entry.durationNs = milliseconds > 0 ? qint64(milliseconds) * 1'000'000 : -1;
    }

    entry.metadata = MetadataState::Loaded;
    return entry;
}

class Batch : public QRunnable
{
public:
    Batch(MetadataReader *reader, QList<QUrl> urls,
          std::shared_ptr<QAtomicInt> generation, int stamp)
        : m_reader(reader)
        , m_urls(std::move(urls))
        , m_generation(std::move(generation))
        , m_stamp(stamp)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QList<PlaylistEntry> results;
        results.reserve(m_urls.size());
        for (const QUrl &url : std::as_const(m_urls)) {
            if (m_generation->loadAcquire() != m_stamp)
                return; // cancelled; drop the work rather than deliver it
            results.append(readOne(url));
        }

        if (m_generation->loadAcquire() != m_stamp)
            return;

        // Queued, so the model is only ever touched on the thread that owns it.
        MetadataReader *reader = m_reader;
        QMetaObject::invokeMethod(
            reader, [reader, results] { emit reader->batchReady(results); },
            Qt::QueuedConnection);
    }

private:
    MetadataReader *m_reader;
    QList<QUrl> m_urls;
    std::shared_ptr<QAtomicInt> m_generation;
    int m_stamp;
};

} // namespace

MetadataReader::MetadataReader(QObject *parent)
    : QObject(parent)
    , m_generation(std::make_shared<QAtomicInt>(0))
{
    m_pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount(), 4));
    m_pool.setObjectName(QStringLiteral("ferrolux-metadata"));
}

MetadataReader::~MetadataReader()
{
    // Invalidate first so running batches abandon early, then block until the
    // pool is genuinely empty. Returning from this destructor while a worker
    // still holds `this` would be a use-after-free on the next invokeMethod.
    m_generation->fetchAndAddOrdered(1);
    m_pool.clear();
    m_pool.waitForDone();
}

void MetadataReader::enqueue(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;

    const int stamp = m_generation->loadAcquire();
    for (int offset = 0; offset < urls.size(); offset += kBatchSize) {
        const QList<QUrl> slice = urls.mid(offset, kBatchSize);
        m_pool.start(new Batch(this, slice, m_generation, stamp));
        ++m_outstanding;
    }
}

void MetadataReader::cancel()
{
    m_generation->fetchAndAddOrdered(1);
    m_pool.clear();
    m_outstanding = 0;
    emit idle();
}

} // namespace ferrolux::library
