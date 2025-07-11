/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEB_RTC)

#include "ActiveDOMObject.h"
#include "Event.h"
#include "EventTarget.h"
#include "EventTargetInterfaces.h"
#include "RTCSctpTransportBackend.h"

namespace WebCore {

class RTCDtlsTransport;

class RTCSctpTransport final : public RefCounted<RTCSctpTransport>, public ActiveDOMObject, public EventTarget, public RTCSctpTransportBackendClient {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RTCSctpTransport);
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    static Ref<RTCSctpTransport> create(ScriptExecutionContext&, UniqueRef<RTCSctpTransportBackend>&&, Ref<RTCDtlsTransport>&&);
    ~RTCSctpTransport();

    RTCDtlsTransport& transport() { return m_transport.get(); }
    RTCSctpTransportState state() const { return m_state; }
    double maxMessageSize() const { return m_maxMessageSize.value_or(std::numeric_limits<double>::infinity()); }
    std::optional<unsigned short>  maxChannels() const { return m_maxChannels; }

    void updateMaxMessageSize(std::optional<double>);

    const RTCSctpTransportBackend& backend() const { return m_backend.get(); }

private:
    RTCSctpTransport(ScriptExecutionContext&, UniqueRef<RTCSctpTransportBackend>&&, Ref<RTCDtlsTransport>&&);

    // EventTarget
    enum EventTargetInterfaceType eventTargetInterface() const final { return EventTargetInterfaceType::RTCSctpTransport; }
    ScriptExecutionContext* scriptExecutionContext() const final { return ActiveDOMObject::scriptExecutionContext(); }
    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }

    // ActiveDOMObject
    void stop() final;
    bool virtualHasPendingActivity() const final;

    // RTCSctpTransport::Client
    void onStateChanged(RTCSctpTransportState, std::optional<double>, std::optional<unsigned short>) final;

    const UniqueRef<RTCSctpTransportBackend> m_backend;
    const Ref<RTCDtlsTransport> m_transport;
    RTCSctpTransportState m_state { RTCSctpTransportState::Connecting };
    std::optional<double> m_maxMessageSize;
    std::optional<unsigned short> m_maxChannels;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
