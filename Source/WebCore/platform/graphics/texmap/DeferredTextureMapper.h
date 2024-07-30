// TODO: copyright

#pragma once

#include "TextureMapper.h"

namespace WebCore {
class DeferredTextureMapper : public TextureMapper {
public:
    DeferredTextureMapper(TextureMapper&);
    ~DeferredTextureMapper() = default;

    void replaceNthDeferredOperation(std::size_t n, std::function<void(TextureMapper*)>&&);
    void executeDeferredOperations();

    // TextureMapper
    void drawBorder(const Color&, float borderWidth, const FloatRect&, const TransformationMatrix&) override;
    void drawNumber(int number, const Color&, const FloatPoint&, const TransformationMatrix&) override;
    void drawTexture(const BitmapTexture&, const FloatRect& target, const TransformationMatrix& modelViewMatrix = TransformationMatrix(), float opacity = 1.0f, AllEdgesExposed = AllEdgesExposed::Yes) override;
    void drawTexture(uint32_t textureId, OptionSet<TextureMapperFlags> textureColorConvertFlags, bool textureIsOpaque, RefPtr<const FilterOperation> textureFilterOperation, const FloatRect& targetRect, const TransformationMatrix&, float opacity, AllEdgesExposed) override;
    void drawTexture(GLuint texture, OptionSet<TextureMapperFlags>, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed = AllEdgesExposed::Yes) override;
    void drawTexturePlanarYUV(const std::array<GLuint, 3>& textures, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags>, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, std::optional<GLuint> alphaPlane, AllEdgesExposed = AllEdgesExposed::Yes) override;
    void drawTextureSemiPlanarYUV(const std::array<GLuint, 2>& textures, bool uvReversed, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags>, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed = AllEdgesExposed::Yes) override;
    void drawTexturePackedYUV(GLuint texture, const std::array<GLfloat, 16>& yuvToRgbMatrix, OptionSet<TextureMapperFlags>, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity, AllEdgesExposed = AllEdgesExposed::Yes) override;
    void drawTextureExternalOES(GLuint texture, OptionSet<TextureMapperFlags>, const FloatRect&, const TransformationMatrix& modelViewMatrix, float opacity) override;
    void drawSolidColor(const FloatRect&, const TransformationMatrix&, const Color&, bool) override;
    void clearColor(const Color&) override;
    void bindSurface(BitmapTexture* surface) override;
    BitmapTexture* currentSurface() override;
    void beginClip(const TransformationMatrix&, const FloatRoundedRect&) override;
    void beginPainting(FlipY = FlipY::No, BitmapTexture* = nullptr) override;
    void endPainting() override;
    void endClip() override;
    IntRect clipBounds() override;
    void setDepthRange(double zNear, double zFar) override;
    void setMaskMode(bool m) override;
    void setWrapMode(WrapMode m) override;
    void setPatternTransform(const TransformationMatrix& p) override;
    RefPtr<BitmapTexture> applyFilters(RefPtr<BitmapTexture>&, const FilterOperations&, bool defersLastPass) override;
    RefPtr<BitmapTexture> acquireTextureFromPool(const IntSize&, OptionSet<BitmapTexture::Flags>) override;

#if USE(GRAPHICS_LAYER_WC)
    void releaseUnusedTexturesNow() override;
#endif

private:
    TextureMapper& m_textureMapper;
    ClipStack m_clipStack;
    BitmapTexture* m_currentSurface { nullptr };
    Vector<std::function<void()>> m_deferredOperations;
};
}

#if USE(TEXTURE_MAPPER)
#endif // USE(TEXTURE_MAPPER)
