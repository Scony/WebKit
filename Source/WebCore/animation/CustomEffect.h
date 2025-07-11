/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "AnimationEffect.h"
#include "CustomEffectCallback.h"
#include "EffectTiming.h"
#include <wtf/Ref.h>

namespace WebCore {

class CustomEffect final : public AnimationEffect {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(CustomEffect);
public:
    static ExceptionOr<Ref<CustomEffect>> create(Document&, Ref<CustomEffectCallback>&&, std::optional<Variant<double, EffectTiming>>&&);
    ~CustomEffect() { }

private:
    CustomEffect(Ref<CustomEffectCallback>&&);

    // AnimationEffect
    bool isCustomEffect() const final { return true; }
    void animationDidTick() final;
    bool ticksContinuouslyWhileActive() const final { return true; }

    const Ref<CustomEffectCallback> m_callback;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_ANIMATION_EFFECT(CustomEffect, isCustomEffect());
