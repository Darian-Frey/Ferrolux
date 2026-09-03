// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "meters/MeterTexture.h"

#include "meters/MeterSource.h"

#include <QQuickWindow>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QRunnable>

#include <algorithm>
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
}

MeterTexture::~MeterTexture() = default;

void MeterTexture::encodeTexel(float magnitude, float peak, uchar *rgba)
{
    const int packed = int(std::lround(double(qBound(0.0f, magnitude, 1.0f)) * 65535.0));
    rgba[0] = uchar((packed >> 8) & 0xFF);
    rgba[1] = uchar(packed & 0xFF);
    rgba[2] = uchar(std::lround(double(qBound(0.0f, peak, 1.0f)) * 255.0));

    // Opaque, so nothing premultiplies the data channels. This is not a
    // precaution; it has been tested. The flame shader wanted a per-frame
    // ceiling to bound its silhouettes, and alpha was the one channel with
    // nothing in it — so it was put there, and 52% of the spectrum display's
    // pixels changed. The scene graph normalises an image with alpha to a
    // premultiplied format on upload, which scales R, G and B by A, and R and G
    // are the magnitude. Any value here but 255 silently corrupts every mode.
    // The ceiling travels as a uniform instead. See BUG-016.
    rgba[3] = 255;
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

// Follows the item into and out of a scene. Connections are direct so that
// synchronise() runs on the render thread that emits the signal, not queued
// back onto the GUI thread where it would be exactly the AV-007 violation this
// design exists to avoid.
void MeterTexture::itemChange(ItemChange change, const ItemChangeData &data)
{
    if (change == ItemSceneChange) {
        if (m_window)
            disconnect(m_window, nullptr, this, nullptr);

        m_window = data.window;

        if (m_window) {
            connect(m_window, &QQuickWindow::beforeSynchronizing,
                    this, &MeterTexture::synchronise, Qt::DirectConnection);
            connect(m_window, &QQuickWindow::sceneGraphInvalidated,
                    this, &MeterTexture::invalidate, Qt::DirectConnection);
        }
    }
    QQuickItem::itemChange(change, data);
}

// Render thread, GUI thread blocked. The only place a texture is created or
// replaced.
void MeterTexture::synchronise()
{
    if (!m_window || m_staging.isNull())
        return;
    if (!m_stagingDirty && textureProvider() && m_provider->texture())
        return;

    auto *provider = static_cast<MeterTextureProvider *>(textureProvider());
    QSGTexture *texture = m_window->createTextureFromImage(m_staging);
    if (texture) {
        // Linear filtering is not decoration: it lets a shader read between
        // band centres and get a smooth spectrum for free, per D-004. The
        // packed magnitude survives it because recombining the two bytes is
        // linear in both, so an interpolated pair decodes to the interpolated
        // value rather than tearing at byte boundaries. Clamping stops the
        // lowest and highest bands wrapping into each other.
        texture->setFiltering(QSGTexture::Linear);
        texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        provider->adopt(texture);
    }
    m_stagingDirty = false;
}

void MeterTexture::invalidate()
{
    delete m_provider;
    m_provider = nullptr;
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
