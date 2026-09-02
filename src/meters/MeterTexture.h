// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// meters/MeterTexture.h — GPU residency for the meter state.
//
// Delivers the second half of F-030 and the mechanism D-004 chose. A
// `ShaderEffect` in QML names this item as a source; the shader then reads band
// values as texels, which buys three things a uniform array would not: sub-pixel
// bar edges from per-fragment evaluation, free interpolation between bands via
// linear filtering, and a logarithmic frequency remap expressed as a coordinate
// transform rather than as CPU work.
//
// Threading — AV-007
// ------------------
// Scene graph resources may only be touched during synchronisation and
// rendering. Band values are staged into a plain image on the GUI thread, and
// the texture is created and updated only inside updatePaintNode(), which the
// scene graph calls on the render thread with the GUI thread blocked. Uploading
// from a bus handler would appear to work under the basic render loop and
// corrupt or crash under the threaded one, which is why BUILD.md tells you to
// force QSG_RENDER_LOOP=threaded while developing.
//
// Texture layout
// --------------
// N×1, one texel per display band.
//
//   R, G   current magnitude, 16 bits, high byte then low
//   B      peak-hold cap, 8 bits
//   A      unused, held at 255
//
// SPEC.md §Meters asks for a two-channel 16-bit texture. Qt's
// createTextureFromImage produces an 8-bit-per-channel texture whatever image
// format it is handed, and reaching past it to the RHI for a genuine RG16
// target is fragile across backends for a texture of at most 48 texels. Packing
// the magnitude across two 8-bit channels delivers the 16 bits the
// specification actually wanted, on a format every backend supports.
//
// Eight bits is left for the peak cap deliberately. Magnitude drives bar height
// and is what a viewer watches decay, where 8-bit steps of 1/255 would be
// visible as stair-stepping on a large display; the cap is a two-pixel line
// whose position 8 bits already resolves past the point of noticing. Alpha is
// held opaque so that nothing in the pipeline can premultiply data channels.

#pragma once

#include <QImage>
#include <QQuickItem>

namespace ferrolux::meters {

class MeterSource;

class MeterTexture : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(ferrolux::meters::MeterSource *source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit MeterTexture(QQuickItem *parent = nullptr);
    ~MeterTexture() override;

    MeterSource *source() const { return m_source; }
    void setSource(MeterSource *source);

    bool isTextureProvider() const override { return true; }
    QSGTextureProvider *textureProvider() const override;

    // The packing described above, as pure functions. A shader's correctness
    // depends on these agreeing exactly, so they are testable without a GPU,
    // a window, or a running scene graph.
    static void encodeTexel(float magnitude, float peak, uchar *rgba);
    static float decodeMagnitude(quint8 red, quint8 green);
    static float decodePeak(quint8 blue);

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void releaseResources() override;

signals:
    void sourceChanged();

private slots:
    void stage();

private:
    MeterSource *m_source = nullptr;

    // Written on the GUI thread, read on the render thread during
    // synchronisation, when the GUI thread is blocked.
    QImage m_staging;
    bool m_stagingDirty = false;

    mutable class MeterTextureProvider *m_provider = nullptr;
};

} // namespace ferrolux::meters
