/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

WI.AnimationObserver = class AnimationObserver extends InspectorBackend.Dispatcher
{
    // Events defined by the "Animation" domain.

    animationCreated(animation)
    {
        WI.animationManager.animationCreated(animation);
    }

    nameChanged(animationId, name)
    {
        WI.animationManager.nameChanged(animationId, name);
    }

    effectChanged(animationId, effect)
    {
        // COMPATIBILITY (iOS 26.0, macOS 26.0): `Animation.effectChanged` removed the `effect` parameter in favor of `Animation.requestEffect`.`
        WI.animationManager.effectChanged(animationId, effect);
    }

    targetChanged(animationId)
    {
        WI.animationManager.targetChanged(animationId);
    }

    animationDestroyed(animationId)
    {
        WI.animationManager.animationDestroyed(animationId);
    }

    trackingStart(timestamp)
    {
        WI.timelineManager.animationTrackingStarted(timestamp);
    }

    trackingUpdate(timestamp, event)
    {
        WI.timelineManager.animationTrackingUpdated(timestamp, event);
    }

    trackingComplete(timestamp)
    {
        WI.timelineManager.animationTrackingCompleted(timestamp);
    }
};
