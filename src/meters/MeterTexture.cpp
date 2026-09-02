// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "meters/MeterTexture.h"

#include "meters/MeterSource.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QRunnable>

#include <cmath>

namespace ferrolux::meters {

// Owns the texture and tells the scene graph when it is replaced. Lives on the
// render thread and is only ever touched there.
class MeterTextureProvider : public QSGTextureProvider
{
public:
    QSGTexture *texture() const override { return m_texture; }

    void adopt(QSGTexture *texture)
    {
        if (m_texture == texture)
            return;
        delete m_texture;
        m_texture = texture;
        emit textureChanged();
    }

    ~MeterTextureProvider() override { delete m_texture; }

private:
    QSGTexture *m_texture = nullptr;
};

// Deletes the provider, and with it the texture, on the render thread at a
// point the scene graph considers safe. Freeing a scene graph resource from the
// GUI thread is the AV-007 failure in its purest form.
class ProviderCleanup : public QRunnable
{
public:
    explicit ProviderCleanup(MeterTextureProvider *provider)
        : m_provider(provider) {}
    void run() override { delete m_provider; }

private:
    MeterTextureProvider *m_provider;
};

MeterTexture::MeterTexture(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

MeterTexture::~MeterTexture() = default;

void MeterTexture::encodeTexel(float magnitude, float peak, uchar *rgba)
{
    const int packed = int(std::lround(double(qBound(0.0f, magnitude, 1.0f)) * 65535.0));
    rgba[0] = uchar((packed >> 8) & 0xFF);
    rgba[1] = uchar(packed & 0xFF);
    rgba[2] = uchar(std::lround(double(qBound(0.0f, peak, 1.0f)) * 255.0));
    rgba[3] = 255; // opaque, so nothing premultiplies the data channels
}

float MeterTexture::decodeMagnitude(quint8 red, quint8 green)
{
    return float((int(red) << 8) | int(green)) / 65535.0f;
}

float MeterTexture::decodePeak(quint8 blue)
{
    return float(blue) / 255.0f;
}

void MeterTexture::setSource(MeterSource *source)
{
    if (m_source == source)
        return;

    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;

    if (m_source) {
        connect(m_source, &MeterSource::updated, this, &MeterTexture::stage);
        connect(m_source, &MeterSource::bandCountChanged, this, &MeterTexture::stage);
        connect(m_source, &QObject::destroyed, this, [this] { m_source = nullptr; });
    }

    stage();
    emit sourceChanged();
}

// GUI thread. Builds the image the render thread will upload; touches no scene
// graph resource, which is the whole point of the split (AV-007).
void MeterTexture::stage()
{
    if (!m_source)
        return;

    const QList<float> &magnitudes = m_source->magnitudes();
    const QList<float> &peaks = m_source->peaks();
    const int bands = int(magnitudes.size());
    if (bands <= 0)
        return;

    if (m_staging.width() != bands || m_staging.height() != 1)
        m_staging = QImage(bands, 1, QImage::Format_RGBA8888);

    auto *texel = reinterpret_cast<uchar *>(m_staging.bits());
    for (int band = 0; band < bands; ++band)
        encodeTexel(magnitudes.at(band), peaks.value(band), texel + band * 4);

    m_stagingDirty = true;
    update();
}

QSGTextureProvider *MeterTexture::textureProvider() const
{
    if (!m_provider)
        m_provider = new MeterTextureProvider;
    return m_provider;
}

// Render thread, during synchronisation with the GUI thread blocked. The only
// place a texture is created or replaced.
QSGNode *MeterTexture::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (m_staging.isNull() || !window()) {
        delete node;
        return nullptr;
    }

    auto *provider = static_cast<MeterTextureProvider *>(textureProvider());

    if (m_stagingDirty || !provider->texture()) {
        QSGTexture *texture = window()->createTextureFromImage(m_staging);
        if (texture) {
            // Linear filtering is not decoration: it is what lets a shader read
            // between band centres and get a smooth spectrum for free, per
            // D-004. Clamping stops the lowest and highest bands wrapping into
            // each other at the edges.
            texture->setFiltering(QSGTexture::Linear);
            texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            provider->adopt(texture);
        }
        m_stagingDirty = false;
    }

    // The item draws nothing itself — it exists to be sampled. A node is still
    // returned so the scene graph keeps calling this on every frame the item is
    // marked dirty.
    auto *textureNode = static_cast<QSGSimpleTextureNode *>(node);
    if (!textureNode)
        textureNode = new QSGSimpleTextureNode;
    textureNode->setTexture(provider->texture());
    textureNode->setRect(0, 0, 0, 0); // zero-sized: sampled, never painted
    textureNode->setOwnsTexture(false);
    return textureNode;
}

void MeterTexture::releaseResources()
{
    if (!m_provider)
        return;

    if (QQuickWindow *host = window()) {
        host->scheduleRenderJob(new ProviderCleanup(m_provider),
                                QQuickWindow::AfterSynchronizingStage);
    } else {
        // No window means no render thread to schedule onto, and therefore
        // nothing else that could still be reading it.
        delete m_provider;
    }
    m_provider = nullptr;
}

} // namespace ferrolux::meters
