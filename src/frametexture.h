#pragma once

#include <QImage>
#include <QSGTexture>
#include <memory>
#include <rhi/qrhi.h>

// Scene graph texture owned and updated exclusively on Qt Quick's render
// thread. Reuse GPU storage across frames; QRhi handles upload synchronization.
class FrameTexture final : public QSGTexture
{
  public:
    static std::unique_ptr<FrameTexture> create(QRhi *rhi, const QImage &image, QRhiTexture::Flags flags = {})
    {
        auto result = std::unique_ptr<FrameTexture>(new FrameTexture);
        result->m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1, flags));
        if (!result->m_texture->create())
            return {};
        result->setImage(image);
        return result;
    }

    void setImage(const QImage &image) { m_pending = image; }
    qint64 comparisonKey() const override { return qint64(quintptr(m_texture.get())); }
    QRhiTexture *rhiTexture() const override { return m_texture.get(); }
    QSize textureSize() const override { return m_texture->pixelSize(); }
    bool hasAlphaChannel() const override { return true; }
    bool hasMipmaps() const override { return false; }
    void commitTextureOperations(QRhi *, QRhiResourceUpdateBatch *updates) override
    {
        if (!m_pending.isNull()) {
            updates->uploadTexture(m_texture.get(), m_pending);
            m_pending = {};
        }
    }

  private:
    std::unique_ptr<QRhiTexture> m_texture;
    QImage m_pending;
};
