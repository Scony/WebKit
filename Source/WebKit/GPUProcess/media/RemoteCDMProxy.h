/*
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS) && ENABLE(ENCRYPTED_MEDIA)

#include "Connection.h"
#include "MessageReceiver.h"
#include "RemoteCDMFactoryProxy.h"
#include "RemoteCDMInstanceIdentifier.h"
#include <WebCore/CDMPrivate.h>
#include <wtf/Forward.h>
#include <wtf/UniqueRef.h>

namespace WebCore {
class SharedBuffer;
enum class CDMRequirement : uint8_t;
enum class CDMSessionType : uint8_t;
struct CDMKeySystemConfiguration;
struct CDMRestrictions;
}

namespace WebKit {

class RemoteCDMInstanceProxy;
struct RemoteCDMInstanceConfiguration;
struct RemoteCDMConfiguration;
struct SharedPreferencesForWebProcess;

class RemoteCDMProxy : public RefCounted<RemoteCDMProxy>, public IPC::MessageReceiver {
public:
    static RefPtr<RemoteCDMProxy> create(RemoteCDMFactoryProxy&, std::unique_ptr<WebCore::CDMPrivate>&&);
    ~RemoteCDMProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    const RemoteCDMConfiguration& configuration() const { return m_configuration.get(); }

    RemoteCDMFactoryProxy* factory() const { return m_factory.get(); }
    RefPtr<RemoteCDMFactoryProxy> protectedFactory() const { return m_factory.get(); }

    bool supportsInitData(const AtomString&, const WebCore::SharedBuffer&);
    RefPtr<WebCore::SharedBuffer> sanitizeResponse(const WebCore::SharedBuffer& response);
    std::optional<String> sanitizeSessionId(const String& sessionId);
    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

#if !RELEASE_LOG_DISABLED
    const Logger& logger() const { return m_logger; }
    uint64_t logIdentifier() const { return m_logIdentifier; }
#endif

private:
    friend class RemoteCDMFactoryProxy;
    RemoteCDMProxy(RemoteCDMFactoryProxy&, std::unique_ptr<WebCore::CDMPrivate>&&, UniqueRef<RemoteCDMConfiguration>&&);

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&) final;

    // Messages
    void getSupportedConfiguration(WebCore::CDMKeySystemConfiguration&&, WebCore::CDMPrivate::LocalStorageAccess, CompletionHandler<void(std::optional<WebCore::CDMKeySystemConfiguration>)>&&);
    void createInstance(CompletionHandler<void(std::optional<RemoteCDMInstanceIdentifier>, RemoteCDMInstanceConfiguration&&)>&&);
    void loadAndInitialize();
    void setLogIdentifier(uint64_t);

    WeakPtr<RemoteCDMFactoryProxy> m_factory;
    const std::unique_ptr<WebCore::CDMPrivate> m_private;
    const UniqueRef<RemoteCDMConfiguration> m_configuration;

#if !RELEASE_LOG_DISABLED
    const Ref<const Logger> m_logger;
    uint64_t m_logIdentifier { 0 };
#endif
};

}

#endif
