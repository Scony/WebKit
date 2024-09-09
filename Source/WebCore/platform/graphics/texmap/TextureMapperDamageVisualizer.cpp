/*
 * Copyright (C) 2024 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(DAMAGE_TRACKING)
#include "TextureMapperDamageVisualizer.h"

#include "Damage.h"
#include "TextureMapper.h"
#include <wtf/text/StringToIntegerConversion.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(TextureMapperDamageVisualizer);

TextureMapperDamageVisualizer::TextureMapperDamageVisualizer()
    : m_active(false)
    , m_margin(0)
{
    if (const auto* showDmageEnvvar = getenv("WEBKIT_SHOW_DAMAGE")) {
        const auto value = parseInteger<unsigned>(StringView::fromLatin1(showDmageEnvvar));
        if (value && *value > 0) {
            m_active = true;
            m_margin = *value - 1;
        }
    }
}

void TextureMapperDamageVisualizer::updateDamageAndDisplay(TextureMapper& textureMapper, const Damage& damage)
{
    if (!m_active || damage.isInvalid())
        return;

    const FloatPoint offset(m_margin, m_margin);
    const FloatSize extraSize(m_margin * 2, m_margin * 2);
    for (const auto& rect : damage.rects()) {
        FloatRect rectToDraw(rect);
        rectToDraw.moveBy(-offset);
        rectToDraw.expand(extraSize);
        textureMapper.drawSolidColor(rectToDraw, { }, SRGBA<uint8_t> { 255, 0, 0, 200 }, true);
    }
}

}

#endif // ENABLE(DAMAGE_TRACKING)
