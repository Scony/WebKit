// TODO: copyright

#include "config.h"
#include "DeferredTextureMapper.h"
#include "FloatQuad.h"
#include "TextureMapperFlags.h"

#if USE(TEXTURE_MAPPER)

namespace WebCore {

DeferredTextureMapper::DeferredTextureMapper(TextureMapper& textureMapper)
    : m_textureMapper(textureMapper)
{
}

void DeferredTextureMapper::replaceNthDeferredOperation(std::size_t n, std::function<void(TextureMapper*)>&& newOperation)
{
    m_deferredOperations[n] = [this, newOperation = WTFMove(newOperation)]() {
        newOperation(&m_textureMapper);
    };
}

void DeferredTextureMapper::executeDeferredOperations()
{
    for (const auto& deferredOperation : m_deferredOperations)
        // FIXME: Skip all operations except endClip() and endPainting() when m_textureMapper.clipBounds().isEmpty().
        deferredOperation();
}

void DeferredTextureMapper::drawBorder(const Color& color, float width, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix)
{
    m_deferredOperations.append([this, color, width, targetRect, modelViewMatrix]() {
        m_textureMapper.drawBorder(color, width, targetRect, modelViewMatrix);
    });
}

void DeferredTextureMapper::drawNumber(int number, const Color& color, const FloatPoint& targetPoint, const TransformationMatrix& modelViewMatrix)
{
    m_deferredOperations.append([this, number, color, targetPoint, modelViewMatrix]() {
        m_textureMapper.drawNumber(number, color, targetPoint, modelViewMatrix);
    });
}

void DeferredTextureMapper::drawTexture(const BitmapTexture& texture, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity, AllEdgesExposed allEdgesExposed)
{
    auto textureId = texture.id();
    auto textureColorConvertFlags = texture.colorConvertFlags();
    auto textureIsOpaque = texture.isOpaque();
    auto textureFilterOperation = texture.filterOperation();
    m_deferredOperations.append([this, textureId, textureColorConvertFlags, textureIsOpaque, textureFilterOperation, targetRect, matrix, opacity, allEdgesExposed]() {
        m_textureMapper.drawTexture(textureId, textureColorConvertFlags, textureIsOpaque, textureFilterOperation, targetRect, matrix, opacity, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTexture(uint32_t textureId, OptionSet<TextureMapperFlags> textureColorConvertFlags, bool textureIsOpaque, RefPtr<const FilterOperation> textureFilterOperation, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity, AllEdgesExposed allEdgesExposed)
{
    m_deferredOperations.append([this, textureId, textureColorConvertFlags, textureIsOpaque, textureFilterOperation, targetRect, matrix, opacity, allEdgesExposed]() {
        m_textureMapper.drawTexture(textureId, textureColorConvertFlags, textureIsOpaque, textureFilterOperation, targetRect, matrix, opacity, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTexture(GLuint texture, OptionSet<TextureMapperFlags> flags, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed allEdgesExposed)
{
    m_deferredOperations.append([this, texture, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed]() {
        m_textureMapper.drawTexture(texture, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTexturePlanarYUV(const std::array<GLuint, 3>& textures, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags> flags, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, std::optional<GLuint> alphaPlane, AllEdgesExposed allEdgesExposed)
{
    m_deferredOperations.append([this, textures, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, alphaPlane, allEdgesExposed]() {
        m_textureMapper.drawTexturePlanarYUV(textures, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, alphaPlane, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTextureSemiPlanarYUV(const std::array<GLuint, 2>& textures, bool uvReversed, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags> flags, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed allEdgesExposed)
{
    m_deferredOperations.append([this, textures, uvReversed, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed]() {
        m_textureMapper.drawTextureSemiPlanarYUV(textures, uvReversed, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTexturePackedYUV(GLuint texture, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags> flags, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed allEdgesExposed)
{
    m_deferredOperations.append([this, texture, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed]() {
        m_textureMapper.drawTexturePackedYUV(texture, yuvToRgbMatrix, flags, targetRect, modelViewMatrix, opacity, allEdgesExposed);
    });
}

void DeferredTextureMapper::drawTextureExternalOES(GLuint texture, OptionSet<TextureMapperFlags> flags, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    m_deferredOperations.append([this, texture, flags, targetRect, modelViewMatrix, opacity]() {
        m_textureMapper.drawTextureExternalOES(texture, flags, targetRect, modelViewMatrix, opacity);
    });
}

void DeferredTextureMapper::drawSolidColor(const FloatRect& rect, const TransformationMatrix& matrix, const Color& color, bool isBlendingAllowed)
{
    m_deferredOperations.append([this, rect, matrix, color, isBlendingAllowed]() {
        m_textureMapper.drawSolidColor(rect, matrix, color, isBlendingAllowed);
    });
}

void DeferredTextureMapper::clearColor(const Color& color)
{
    m_deferredOperations.append([this, color]() {
        m_textureMapper.clearColor(color);
    });
}

void DeferredTextureMapper::bindSurface(BitmapTexture* surface)
{
    m_deferredOperations.append([this, surface]() {
        m_textureMapper.bindSurface(surface);
    });
    if (surface)
        m_currentSurface = surface;
}

BitmapTexture* DeferredTextureMapper::currentSurface()
{
    return m_currentSurface;
}

void DeferredTextureMapper::beginClip(const TransformationMatrix& modelViewMatrix, const FloatRoundedRect& targetRect)
{
    m_deferredOperations.append([this, modelViewMatrix, targetRect]() { m_textureMapper.beginClip(modelViewMatrix, targetRect); });

    m_clipStack.push();

    // Try rounded rect clip.
    if (targetRect.isRounded() && targetRect.isRenderable() && !targetRect.isEmpty() && modelViewMatrix.isInvertible() && m_clipStack.isRoundedRectClipAllowed()) {
        FloatQuad quad = modelViewMatrix.projectQuad(targetRect.rect());
        IntRect rect = quad.enclosingBoundingBox();
        m_clipStack.addRoundedRect(targetRect, modelViewMatrix.inverse().value());
        m_clipStack.intersect(rect);
        return;
    }

    // Try scissor clip.
    if (modelViewMatrix.isAffine()) {
        FloatQuad quad = modelViewMatrix.projectQuad(targetRect.rect());
        IntRect rect = quad.enclosingBoundingBox();
        if (quad.isRectilinear() && !rect.isEmpty())
            m_clipStack.intersect(rect);
    }
}

void DeferredTextureMapper::beginPainting(FlipY flipY, BitmapTexture* surface)
{
    m_deferredOperations.append([this, flipY, surface]() { m_textureMapper.beginPainting(flipY, surface); });
    m_clipStack.reset(m_textureMapper.clipBounds(), flipY == FlipY::Yes ? ClipStack::YAxisMode::Default : ClipStack::YAxisMode::Inverted);
}

void DeferredTextureMapper::endPainting()
{
    m_deferredOperations.append([&]() { m_textureMapper.endPainting(); });
}

void DeferredTextureMapper::endClip()
{
    m_deferredOperations.append([&]() { m_textureMapper.endClip(); });
    m_clipStack.pop();
}

IntRect DeferredTextureMapper::clipBounds()
{
    return m_clipStack.current().scissorBox;
}

void DeferredTextureMapper::setDepthRange(double zNear, double zFar)
{
    m_deferredOperations.append([this, zNear, zFar]() {
        m_textureMapper.setDepthRange(zNear, zFar);
    });
}

void DeferredTextureMapper::setMaskMode(bool maskMode)
{
    m_deferredOperations.append([this, maskMode]() {
        m_textureMapper.setMaskMode(maskMode);
    });
}

void DeferredTextureMapper::setWrapMode(WrapMode wrapMode)
{
    m_deferredOperations.append([this, wrapMode]() {
        m_textureMapper.setWrapMode(wrapMode);
    });
}

void DeferredTextureMapper::setPatternTransform(const TransformationMatrix& transform)
{
    m_deferredOperations.append([this, transform]() {
        m_textureMapper.setPatternTransform(transform);
    });
}

RefPtr<BitmapTexture> DeferredTextureMapper::applyFilters(RefPtr<BitmapTexture>& sourceTexture, const FilterOperations& filters, bool defersLastPass)
{
    UNUSED_PARAM(filters);
    UNUSED_PARAM(defersLastPass);
    WTFLogAlways("DeferredTextureMapper::applyFilters");
    WTFReportBacktrace();
    return sourceTexture;       // TODO: not sure if doable
}

RefPtr<BitmapTexture> DeferredTextureMapper::acquireTextureFromPool(const IntSize& size, OptionSet<BitmapTexture::Flags> flags)
{
    return m_textureMapper.acquireTextureFromPool(size, flags);
}

}

#endif // USE(TEXTURE_MAPPER)
