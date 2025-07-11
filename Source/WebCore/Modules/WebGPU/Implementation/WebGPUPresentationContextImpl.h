/*
 * Copyright (C) 2022-2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if HAVE(WEBGPU_IMPLEMENTATION)

#include "WebGPUIntegralTypes.h"
#include "WebGPUPresentationContext.h"
#include "WebGPUPtr.h"
#include "WebGPUTextureFormat.h"
#include <IOSurface/IOSurfaceRef.h>
#include <WebGPU/WebGPU.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore::WebGPU {

class ConvertToBackingContext;
class TextureImpl;

class PresentationContextImpl final : public PresentationContext {
    WTF_MAKE_TZONE_ALLOCATED(PresentationContextImpl);
public:
    static Ref<PresentationContextImpl> create(WebGPUPtr<WGPUSurface>&& surface, ConvertToBackingContext& convertToBackingContext)
    {
        return adoptRef(*new PresentationContextImpl(WTFMove(surface), convertToBackingContext));
    }

    virtual ~PresentationContextImpl();

    void setSize(uint32_t width, uint32_t height);

    void present(uint32_t frameIndex, bool = false);

    WGPUSurface backing() const { return m_backing.get(); }
    RefPtr<WebCore::NativeImage> getMetalTextureAsNativeImage(uint32_t bufferIndex, bool& isIOSurfaceSupportedFormat) final;

private:
    friend class DowncastConvertToBackingContext;

    PresentationContextImpl(WebGPUPtr<WGPUSurface>&&, ConvertToBackingContext&);

    PresentationContextImpl(const PresentationContextImpl&) = delete;
    PresentationContextImpl(PresentationContextImpl&&) = delete;
    PresentationContextImpl& operator=(const PresentationContextImpl&) = delete;
    PresentationContextImpl& operator=(PresentationContextImpl&&) = delete;

    bool configure(const CanvasConfiguration&) final;
    void unconfigure() final;

    RefPtr<Texture> getCurrentTexture(uint32_t) final;

    TextureFormat m_format { TextureFormat::Bgra8unorm };
    uint32_t m_width { 0 };
    uint32_t m_height { 0 };

    WebGPUPtr<WGPUSurface> m_backing;
    WebGPUPtr<WGPUSwapChain> m_swapChain;
    const Ref<ConvertToBackingContext> m_convertToBackingContext;
    RefPtr<TextureImpl> m_currentTexture;
};

} // namespace WebCore::WebGPU

#endif // HAVE(WEBGPU_IMPLEMENTATION)
