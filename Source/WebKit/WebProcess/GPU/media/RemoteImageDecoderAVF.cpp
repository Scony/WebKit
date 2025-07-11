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

#include "config.h"
#include "RemoteImageDecoderAVF.h"

#if ENABLE(GPU_PROCESS) && HAVE(AVASSETREADER)

#include "GPUProcessConnection.h"
#include "RemoteImageDecoderAVFProxyMessages.h"
#include "SharedBufferReference.h"
#include "WebProcess.h"
#include <WebCore/AVAssetMIMETypeCache.h>
#include <WebCore/DestinationColorSpace.h>
#include <WebCore/IOSurface.h>
#include <WebCore/ImageTypes.h>
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/UTIUtilities.h>
#include <wtf/MachSendRight.h>

namespace WebKit {

using namespace WebCore;

RemoteImageDecoderAVF::RemoteImageDecoderAVF(RemoteImageDecoderAVFManager& manager, const WebCore::ImageDecoderIdentifier& identifier, const String& mimeType)
    : ImageDecoder()
    , m_gpuProcessConnection(manager.ensureGPUProcessConnection())
    , m_manager(manager)
    , m_identifier(identifier)
    , m_mimeType(mimeType)
    , m_uti(WebCore::UTIFromMIMEType(mimeType))
{
}

RemoteImageDecoderAVF::~RemoteImageDecoderAVF()
{
    protectedManager()->deleteRemoteImageDecoder(m_identifier);
}

Ref<RemoteImageDecoderAVFManager> RemoteImageDecoderAVF::protectedManager() const
{
    return m_manager.get().releaseNonNull();
}

bool RemoteImageDecoderAVF::canDecodeType(const String& mimeType)
{
    // TODO: We may need to remove AVAssetMIMETypeCache in the Web process
    return AVAssetMIMETypeCache::singleton().canDecodeType(mimeType) != MediaPlayerEnums::SupportsType::IsNotSupported;
}

bool RemoteImageDecoderAVF::supportsMediaType(MediaType type)
{
    // TODO: We may need to remove AVAssetMIMETypeCache in the Web process
    return type == MediaType::Video && AVAssetMIMETypeCache::singleton().isAvailable();
}

EncodedDataStatus RemoteImageDecoderAVF::encodedDataStatus() const
{
    if (m_frameCount)
        return EncodedDataStatus::Complete;
    if (m_size)
        return EncodedDataStatus::SizeAvailable;
    if (m_hasTrack)
        return EncodedDataStatus::TypeAvailable;

    return EncodedDataStatus::Unknown;
}

void RemoteImageDecoderAVF::setEncodedDataStatusChangeCallback(WTF::Function<void(EncodedDataStatus)>&& callback)
{
    m_encodedDataStatusChangedCallback = WTFMove(callback);
}

IntSize RemoteImageDecoderAVF::size() const
{
    if (m_size)
        return m_size.value();
    return IntSize();
}

size_t RemoteImageDecoderAVF::frameCount() const
{
    return m_frameCount;
}

RepetitionCount RemoteImageDecoderAVF::repetitionCount() const
{
    return m_frameCount > 1 ? RepetitionCountInfinite : RepetitionCountNone;
}

String RemoteImageDecoderAVF::uti() const
{
    return m_uti;
}

String RemoteImageDecoderAVF::filenameExtension() const
{
    return MIMETypeRegistry::preferredExtensionForMIMEType(m_mimeType);
}

IntSize RemoteImageDecoderAVF::frameSizeAtIndex(size_t, SubsamplingLevel) const
{
    return size();
}

bool RemoteImageDecoderAVF::frameIsCompleteAtIndex(size_t index) const
{
    return index < m_frameCount;
}

Seconds RemoteImageDecoderAVF::frameDurationAtIndex(size_t index) const
{
    if (m_frameInfos.isEmpty() || index >= m_frameInfos.size())
        return { };

    return m_frameInfos[index].duration;
}

bool RemoteImageDecoderAVF::frameHasAlphaAtIndex(size_t index) const
{
    if (m_frameInfos.isEmpty() || index >= m_frameInfos.size())
        return false;

    return m_frameInfos[index].hasAlpha;
}

PlatformImagePtr RemoteImageDecoderAVF::createFrameImageAtIndex(size_t index, SubsamplingLevel, const DecodingOptions&)
{
    if (m_frameImages.contains(index))
        return m_frameImages.get(index);

    Ref protectedThis { *this };
    auto createFrameImage = [&protectedThis, index] {
        RefPtr gpuProcessConnection = protectedThis->m_gpuProcessConnection.get();
        if (!gpuProcessConnection)
            return;

        auto sendResult = gpuProcessConnection->connection().sendSync(Messages::RemoteImageDecoderAVFProxy::CreateFrameImageAtIndex(protectedThis->m_identifier, index), 0);
        auto [imageHandle] = sendResult.takeReplyOr(std::nullopt);
        if (!imageHandle)
            return;

        imageHandle->takeOwnershipOfMemory(MemoryLedger::Graphics);

        if (RefPtr bitmap = ShareableBitmap::create(WTFMove(*imageHandle)))
            protectedThis->m_frameImages.add(index, bitmap->makeCGImage());
    };

    callOnMainRunLoopAndWait(WTFMove(createFrameImage));

    if (m_frameImages.contains(index))
        return m_frameImages.get(index);

    return nullptr;
}

void RemoteImageDecoderAVF::setExpectedContentSize(long long expectedContentSize)
{
    auto gpuProcessConnection = m_gpuProcessConnection.get();
    if (!gpuProcessConnection)
        return;

    gpuProcessConnection->connection().send(Messages::RemoteImageDecoderAVFProxy::SetExpectedContentSize(m_identifier, expectedContentSize), 0);
}

// If allDataReceived is true, the caller expects encodedDataStatus() to be >= EncodedDataStatus::SizeAvailable
// after this function returns (in the same run loop).
void RemoteImageDecoderAVF::setData(const FragmentedSharedBuffer& data, bool allDataReceived)
{
    auto gpuProcessConnection = m_gpuProcessConnection.get();
    if (!gpuProcessConnection)
        return;

    auto sendResult = gpuProcessConnection->connection().sendSync(Messages::RemoteImageDecoderAVFProxy::SetData(m_identifier, IPC::SharedBufferReference(data), allDataReceived), 0);
    if (!sendResult.succeeded())
        return;
    auto [frameCount, size, hasTrack, frameInfos] = sendResult.takeReply();

    m_isAllDataReceived = allDataReceived;
    m_frameCount = frameCount;
    if (!size.isEmpty())
        m_size = size;
    m_hasTrack = hasTrack;

    if (frameInfos)
        m_frameInfos.swap(*frameInfos);
}

void RemoteImageDecoderAVF::clearFrameBufferCache(size_t index)
{
    for (size_t i = 0; i < std::min(index + 1, m_frameCount); ++i)
        m_frameImages.remove(i);

    if (auto gpuProcessConnection = m_gpuProcessConnection.get())
        gpuProcessConnection->connection().send(Messages::RemoteImageDecoderAVFProxy::ClearFrameBufferCache(m_identifier, index), 0);
}

void RemoteImageDecoderAVF::encodedDataStatusChanged(size_t frameCount, const WebCore::IntSize& size, bool hasTrack)
{
    m_frameCount = frameCount;
    if (!size.isEmpty())
        m_size = size;
    m_hasTrack = hasTrack;

    if (m_encodedDataStatusChangedCallback)
        m_encodedDataStatusChangedCallback(encodedDataStatus());
}

}

#endif
