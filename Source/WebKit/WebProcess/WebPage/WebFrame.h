/*
 * Copyright (C) 2010-2022 Apple Inc. All rights reserved.
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

#include "APIObject.h"
#include "DownloadID.h"
#include "IdentifierTypes.h"
#include "MessageReceiver.h"
#include "MessageSender.h"
#include "PolicyDecision.h"
#include "TransactionID.h"
#include "WKBase.h"
#include "WebLocalFrameLoaderClient.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <JavaScriptCore/JSBase.h>
#include <WebCore/AdvancedPrivacyProtections.h>
#include <WebCore/FrameLoaderTypes.h>
#include <WebCore/HitTestRequest.h>
#include <WebCore/LayerHostingContextIdentifier.h>
#include <WebCore/LocalFrameLoaderClient.h>
#include <WebCore/MarkupExclusionRule.h>
#include <WebCore/ProcessIdentifier.h>
#include <WebCore/ShareableBitmap.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/RetainPtr.h>
#include <wtf/WeakPtr.h>

namespace API {
class Array;
}

namespace WebCore {
class CertificateInfo;
class Frame;
class FrameTreeSyncData;
class HTMLFrameOwnerElement;
class HandleUserInputEventResult;
class IntPoint;
class IntRect;
class LocalFrame;
class PlatformMouseEvent;
class RemoteFrame;

enum class FocusDirection : uint8_t;
enum class FoundElementInRemoteFrame : bool;

struct FocusEventData;
struct GlobalWindowIdentifier;
}

namespace WebKit {

class InjectedBundleCSSStyleDeclarationHandle;
class InjectedBundleHitTestResult;
class InjectedBundleNodeHandle;
class InjectedBundleRangeHandle;
class InjectedBundleScriptWorld;
class WebKeyboardEvent;
class WebImage;
class WebMouseEvent;
class WebPage;
class WebRemoteFrameClient;

struct FrameInfoData;
struct FrameTreeNodeData;
struct ProvisionalFrameCreationParameters;
struct WebsitePoliciesData;

enum class WithCertificateInfo : bool { No, Yes };

class WebFrame : public API::ObjectImpl<API::Object::Type::BundleFrame>, public IPC::MessageReceiver, public IPC::MessageSender {
public:
    static Ref<WebFrame> create(WebPage& page, WebCore::FrameIdentifier frameID) { return adoptRef(*new WebFrame(page, frameID)); }
    static Ref<WebFrame> createSubframe(WebPage&, WebFrame& parent, const AtomString& frameName, WebCore::HTMLFrameOwnerElement&);
    static Ref<WebFrame> createRemoteSubframe(WebPage&, WebFrame& parent, WebCore::FrameIdentifier, const String& frameName, std::optional<WebCore::FrameIdentifier> openerFrameID, Ref<WebCore::FrameTreeSyncData>&&);
    ~WebFrame();

    void ref() const final { API::ObjectImpl<API::Object::Type::BundleFrame>::ref(); }
    void deref() const final { API::ObjectImpl<API::Object::Type::BundleFrame>::deref(); }

    void initWithCoreMainFrame(WebPage&, WebCore::Frame&);

    // Called when the FrameLoaderClient (and therefore the WebCore::Frame) is being torn down.
    void invalidate();
    ScopeExit<Function<void()>> makeInvalidator();

    WebPage* page() const;
    RefPtr<WebPage> protectedPage() const;

    static WebFrame* webFrame(std::optional<WebCore::FrameIdentifier>);
    static RefPtr<WebFrame> fromCoreFrame(const WebCore::Frame&);
    WebCore::LocalFrame* coreLocalFrame() const;
    RefPtr<WebCore::LocalFrame> protectedCoreLocalFrame() const;
    WebCore::RemoteFrame* coreRemoteFrame() const;
    WebCore::Frame* coreFrame() const;
    RefPtr<WebCore::Frame> protectedCoreFrame() const;

    void createProvisionalFrame(ProvisionalFrameCreationParameters&&);
    void commitProvisionalFrame();
    void destroyProvisionalFrame();
    void loadDidCommitInAnotherProcess(std::optional<WebCore::LayerHostingContextIdentifier>);
    WebCore::LocalFrame* provisionalFrame() { return m_provisionalFrame.get(); }

    Awaitable<std::optional<FrameInfoData>> getFrameInfo();
    FrameInfoData info(WithCertificateInfo = WithCertificateInfo::No) const;
    FrameTreeNodeData frameTreeData() const;

    WebCore::FrameIdentifier frameID() const { return m_frameID; }

    enum class ForNavigationAction : bool { No, Yes };
    uint64_t setUpPolicyListener(WebCore::FramePolicyFunction&&, ForNavigationAction);
    void invalidatePolicyListeners();
    void didReceivePolicyDecision(uint64_t listenerID, PolicyDecision&&);

    void didFinishLoadInAnotherProcess();
    void removeFromTree();

    void startDownload(const WebCore::ResourceRequest&, const String& suggestedName = { }, WebCore::FromDownloadAttribute = WebCore::FromDownloadAttribute::No);
    void convertMainResourceLoadToDownload(WebCore::DocumentLoader*, const WebCore::ResourceRequest&, const WebCore::ResourceResponse&);

    void addConsoleMessage(MessageSource, MessageLevel, const String&, uint64_t requestID = 0);

    String source() const;
    String contentsAsString() const;
    String selectionAsString() const;

    WebCore::IntSize size() const;

    // WKBundleFrame API and SPI functions
    bool isMainFrame() const;
    bool isRootFrame() const;
    String name() const;
    URL url() const;
    WebCore::CertificateInfo certificateInfo() const;
    String innerText() const;
    bool isFrameSet() const;
    RefPtr<WebFrame> parentFrame() const;
    Ref<API::Array> childFrames();
    JSGlobalContextRef jsContext();
    JSGlobalContextRef jsContextForWorld(WebCore::DOMWrapperWorld&);
    JSGlobalContextRef jsContextForWorld(InjectedBundleScriptWorld*);
    JSGlobalContextRef jsContextForServiceWorkerWorld(WebCore::DOMWrapperWorld&);
    JSGlobalContextRef jsContextForServiceWorkerWorld(InjectedBundleScriptWorld*);
    WebCore::IntRect contentBounds() const;
    WebCore::IntRect visibleContentBounds() const;
    WebCore::IntRect visibleContentBoundsExcludingScrollbars() const;
    WebCore::IntSize scrollOffset() const;
    bool hasHorizontalScrollbar() const;
    bool hasVerticalScrollbar() const;

    static constexpr OptionSet<WebCore::HitTestRequest::Type> defaultHitTestRequestTypes()
    {
        return {{
            WebCore::HitTestRequest::Type::ReadOnly,
            WebCore::HitTestRequest::Type::Active,
            WebCore::HitTestRequest::Type::IgnoreClipping,
            WebCore::HitTestRequest::Type::AllowChildFrameContent,
            WebCore::HitTestRequest::Type::DisallowUserAgentShadowContent,
        }};
    }

    RefPtr<InjectedBundleHitTestResult> hitTest(const WebCore::IntPoint, OptionSet<WebCore::HitTestRequest::Type> = defaultHitTestRequestTypes()) const;

    bool getDocumentBackgroundColor(double* red, double* green, double* blue, double* alpha);
    bool containsAnyFormElements() const;
    bool containsAnyFormControls() const;
    void stopLoading();
    void setAccessibleName(const AtomString&);

    static RefPtr<WebFrame> frameForContext(JSContextRef);
    static RefPtr<WebFrame> contentFrameForWindowOrFrameElement(JSContextRef, JSValueRef);

    JSValueRef jsWrapperForWorld(InjectedBundleCSSStyleDeclarationHandle*, InjectedBundleScriptWorld*);
    JSValueRef jsWrapperForWorld(InjectedBundleNodeHandle*, InjectedBundleScriptWorld*);
    JSValueRef jsWrapperForWorld(InjectedBundleRangeHandle*, InjectedBundleScriptWorld*);

    static String counterValue(JSObjectRef element);

    String layerTreeAsText() const;
    
    unsigned pendingUnloadCount() const;
    
    bool allowsFollowingLink(const URL&) const;

    String provisionalURL() const;
    String suggestedFilenameForResourceWithURL(const URL&) const;
    String mimeTypeForResourceWithURL(const URL&) const;

    void setTextDirection(const String&);
    void updateRemoteFrameSize(WebCore::IntSize);
    void updateFrameSize(WebCore::IntSize);

#if PLATFORM(COCOA)
    typedef bool (*FrameFilterFunction)(WKBundleFrameRef, WKBundleFrameRef subframe, void* context);
    RetainPtr<CFDataRef> webArchiveData(FrameFilterFunction, void* context, const Vector<WebCore::MarkupExclusionRule>& exclusionRules = { }, const String& mainResourceFileName = { });
#endif

    RefPtr<WebImage> createSelectionSnapshot() const;

#if PLATFORM(IOS_FAMILY)
    std::optional<TransactionID> firstLayerTreeTransactionIDAfterDidCommitLoad() const { return m_firstLayerTreeTransactionIDAfterDidCommitLoad; }
    void setFirstLayerTreeTransactionIDAfterDidCommitLoad(TransactionID transactionID) { m_firstLayerTreeTransactionIDAfterDidCommitLoad = transactionID; }
#endif

    WebLocalFrameLoaderClient* localFrameLoaderClient() const;
    RefPtr<WebLocalFrameLoaderClient> protectedLocalFrameLoaderClient() const;

    WebRemoteFrameClient* remoteFrameClient() const;
    WebFrameLoaderClient* frameLoaderClient() const;

#if ENABLE(APP_BOUND_DOMAINS)
    bool shouldEnableInAppBrowserPrivacyProtections();
    void setIsNavigatingToAppBoundDomain(std::optional<NavigatingToAppBoundDomain> isNavigatingToAppBoundDomain) { m_isNavigatingToAppBoundDomain = isNavigatingToAppBoundDomain; };
    std::optional<NavigatingToAppBoundDomain> isNavigatingToAppBoundDomain() const { return m_isNavigatingToAppBoundDomain; }
    std::optional<NavigatingToAppBoundDomain> isTopFrameNavigatingToAppBoundDomain() const;
#endif

    void setIsSafeBrowsingCheckOngoing(SafeBrowsingCheckOngoing isSafeBrowsingCheckOngoing) { m_isSafeBrowsingCheckOngoing = isSafeBrowsingCheckOngoing; };
    SafeBrowsingCheckOngoing isSafeBrowsingCheckOngoing() const { return m_isSafeBrowsingCheckOngoing; }

    Markable<WebCore::LayerHostingContextIdentifier> layerHostingContextIdentifier() { return m_layerHostingContextIdentifier; }

    OptionSet<WebCore::AdvancedPrivacyProtections> advancedPrivacyProtections() const;
    std::optional<OptionSet<WebCore::AdvancedPrivacyProtections>> originatorAdvancedPrivacyProtections() const;

    bool handleContextMenuEvent(const WebCore::PlatformMouseEvent&);
    WebCore::HandleUserInputEventResult handleMouseEvent(const WebMouseEvent&);
    bool handleKeyEvent(const WebKeyboardEvent&);

    bool isFocused() const;

    String frameTextForTesting(bool);

    void markAsRemovedInAnotherProcess() { m_wasRemovedInAnotherProcess = true; }
    bool wasRemovedInAnotherProcess() const { return m_wasRemovedInAnotherProcess; }

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);
    static void sendCancelReply(IPC::Connection&, IPC::Decoder&);

    void setAppBadge(const WebCore::SecurityOriginData&, std::optional<uint64_t> badge);

    std::optional<WebCore::ResourceResponse> resourceResponseForURL(const URL&) const;

private:
    WebFrame(WebPage&, WebCore::FrameIdentifier);

    IPC::Connection* messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;

    void setLayerHostingContextIdentifier(WebCore::LayerHostingContextIdentifier identifier) { m_layerHostingContextIdentifier = identifier; }
    void updateLocalFrameSize(WebCore::LocalFrame&, WebCore::IntSize);

    inline WebCore::DocumentLoader* policySourceDocumentLoader() const;

    RefPtr<WebCore::LocalFrame> localFrame();

    void findFocusableElementDescendingIntoRemoteFrame(WebCore::FocusDirection, const WebCore::FocusEventData&, CompletionHandler<void(WebCore::FoundElementInRemoteFrame)>&&);

    WeakPtr<WebCore::Frame> m_coreFrame;
    WeakPtr<WebPage> m_page;
    RefPtr<WebCore::LocalFrame> m_provisionalFrame;

    struct PolicyCheck {
        ForNavigationAction forNavigationAction { ForNavigationAction::No };
        WebCore::FramePolicyFunction policyFunction;
    };
    HashMap<uint64_t, PolicyCheck> m_pendingPolicyChecks;

    std::optional<DownloadID> m_policyDownloadID;

    const WebCore::FrameIdentifier m_frameID;
    bool m_wasRemovedInAnotherProcess { false };

#if PLATFORM(IOS_FAMILY)
    std::optional<TransactionID> m_firstLayerTreeTransactionIDAfterDidCommitLoad;
#endif
    std::optional<NavigatingToAppBoundDomain> m_isNavigatingToAppBoundDomain;
    SafeBrowsingCheckOngoing m_isSafeBrowsingCheckOngoing { SafeBrowsingCheckOngoing::No };
    Markable<WebCore::LayerHostingContextIdentifier> m_layerHostingContextIdentifier;
    Markable<WebCore::FrameIdentifier> m_frameIDBeforeProvisionalNavigation;
};

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::WebFrame) \
    static bool isType(const API::Object& object) { return object.type() == API::Object::Type::BundleFrame; } \
SPECIALIZE_TYPE_TRAITS_END()
