/*
 *  Copyright (C) 2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2006 Jon Shier (jshier@iastate.edu)
 *  Copyright (C) 2003-2025 Apple Inc. All rights reseved.
 *  Copyright (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 *  Copyright (c) 2015 Canon Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include "config.h"
#include "JSDOMWindowBase.h"

#include "Chrome.h"
#include "CommonVM.h"
#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "Element.h"
#include "Event.h"
#include "EventLoop.h"
#include "FetchResponse.h"
#include "HTMLFrameOwnerElement.h"
#include "InspectorController.h"
#include "JSDOMBindingSecurity.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMWindowCustom.h"
#include "JSDocument.h"
#include "JSExecState.h"
#include "JSFetchResponse.h"
#include "JSNode.h"
#include "JSTrustedScript.h"
#include "LocalFrame.h"
#include "Logging.h"
#include "Microtasks.h"
#include "Page.h"
#include "Quirks.h"
#include "RejectedPromiseTracker.h"
#include "ScriptController.h"
#include "ScriptModuleLoader.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include "TrustedType.h"
#include "WebCoreJSClientData.h"
#include <JavaScriptCore/CodeBlock.h>
#include <JavaScriptCore/DeferredWorkTimer.h>
#include <JavaScriptCore/GlobalObjectMethodTable.h>
#include <JavaScriptCore/JSInternalPromise.h>
#include <JavaScriptCore/JSWebAssembly.h>
#include <JavaScriptCore/Microtask.h>
#include <JavaScriptCore/StrongInlines.h>
#include <JavaScriptCore/VMTrapsInlines.h>
#include <wtf/Language.h>
#include <wtf/MainThread.h>
#include <wtf/RuntimeApplicationChecks.h>

#if PLATFORM(IOS_FAMILY)
#include "ChromeClient.h"
#endif

#if PLATFORM(COCOA)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

namespace WebCore {

using namespace JSC;

const ClassInfo JSDOMWindowBase::s_info = { "Window"_s, &JSDOMGlobalObject::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSDOMWindowBase) };

const GlobalObjectMethodTable* JSDOMWindowBase::globalObjectMethodTable()
{
    static constexpr GlobalObjectMethodTable table = {
        supportsRichSourceInfo,
        shouldInterruptScript,
        javaScriptRuntimeFlags,
        queueMicrotaskToEventLoop,
        shouldInterruptScriptBeforeTimeout,
        moduleLoaderImportModule,
        moduleLoaderResolve,
        moduleLoaderFetch,
        moduleLoaderCreateImportMetaProperties,
        moduleLoaderEvaluate,
        promiseRejectionTracker,
        reportUncaughtExceptionAtEventLoop,
        currentScriptExecutionOwner,
        scriptExecutionStatus,
        reportViolationForUnsafeEval,
        [] { return defaultLanguage(); },
#if ENABLE(WEBASSEMBLY)
        compileStreaming,
        instantiateStreaming,
#else
        nullptr,
        nullptr,
#endif
        deriveShadowRealmGlobalObject,
        codeForEval,
        canCompileStrings,
        trustedScriptStructure,
    };
    return &table;
};

JSDOMWindowBase::JSDOMWindowBase(VM& vm, Structure* structure, RefPtr<DOMWindow>&& window, JSWindowProxy* proxy)
    : JSDOMGlobalObject(vm, structure, proxy->world(), globalObjectMethodTable())
    , m_wrapped(WTFMove(window))
{
    m_proxy.set(vm, this, proxy);
}

JSDOMWindowBase::~JSDOMWindowBase() = default;

DOMWindow& JSDOMWindowBase::wrapped() const
{
    return *m_wrapped;
}

SUPPRESS_ASAN inline void JSDOMWindowBase::initStaticGlobals(JSC::VM& vm)
{
    auto& builtinNames = WebCore::builtinNames(vm);

    GlobalPropertyInfo staticGlobals[] = {
        GlobalPropertyInfo(builtinNames.documentPublicName(), jsNull(), PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly),
        GlobalPropertyInfo(builtinNames.windowPublicName(), m_proxy.get(), PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly),
    };

    addStaticGlobals(staticGlobals, std::size(staticGlobals));
}

void JSDOMWindowBase::finishCreation(VM& vm, JSWindowProxy* proxy)
{
    Base::finishCreation(vm, proxy);
    ASSERT(inherits(info()));

    initStaticGlobals(vm);

    if (m_wrapped && m_wrapped->frame() && m_wrapped->frame()->settings().needsSiteSpecificQuirks())
        setNeedsSiteSpecificQuirks(true);

    if (m_wrapped && ((m_wrapped->frame() && m_wrapped->frame()->settings().showModalDialogEnabled()) || (m_wrapped->documentIfLocal() && m_wrapped->documentIfLocal()->quirks().shouldExposeShowModalDialog())))
        putDirectCustomAccessor(vm, builtinNames(vm).showModalDialogPublicName(), CustomGetterSetter::create(vm, showModalDialogGetter, nullptr), enumToUnderlyingType(PropertyAttribute::CustomValue));
}

void JSDOMWindowBase::destroy(JSCell* cell)
{
    static_cast<JSDOMWindowBase*>(cell)->JSDOMWindowBase::~JSDOMWindowBase();
}

void JSDOMWindowBase::updateDocument()
{
    // Since "document" property is defined as { configurable: false, writable: false, enumerable: true },
    // users cannot change its attributes further.
    // Reaching here, the attributes of "document" property should be never changed.
    ASSERT(m_wrapped->documentIfLocal());
    JSGlobalObject* lexicalGlobalObject = this;
    VM& vm = lexicalGlobalObject->vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

    bool shouldThrowReadOnlyError = false;
    bool ignoreReadOnlyErrors = true;
    bool putResult = false;
    symbolTablePutTouchWatchpointSet(this, lexicalGlobalObject, builtinNames(vm).documentPublicName(), toJS(lexicalGlobalObject, this, m_wrapped->documentIfLocal()), shouldThrowReadOnlyError, ignoreReadOnlyErrors, putResult);
    EXCEPTION_ASSERT_UNUSED(scope, !scope.exception());
}

Document* JSDOMWindowBase::scriptExecutionContext() const
{
    return m_wrapped->documentIfLocal();
}

void JSDOMWindowBase::printErrorMessage(const String& message) const
{
    printErrorMessageForFrame(dynamicDowncast<LocalFrame>(wrapped().frame()), message);
}

bool JSDOMWindowBase::supportsRichSourceInfo(const JSGlobalObject* object)
{
    const JSDOMWindowBase* thisObject = static_cast<const JSDOMWindowBase*>(object);
    auto* frame = thisObject->wrapped().frame();
    if (!frame)
        return false;

    Page* page = frame->page();
    if (!page)
        return false;

    bool enabled = page->inspectorController().enabled();
    ASSERT(enabled || !thisObject->debugger());
    return enabled;
}

static inline bool shouldInterruptScriptToPreventInfiniteRecursionWhenClosingPage(Page* page)
{
    // See <rdar://problem/5479443>. We don't think that page can ever be NULL
    // in this case, but if it is, we've gotten into a lexicalGlobalObject where we may have
    // hung the UI, with no way to ask the client whether to cancel execution.
    // For now, our solution is just to cancel execution no matter what,
    // ensuring that we never hang. We might want to consider other solutions
    // if we discover problems with this one.
    ASSERT(page);
    return !page;
}

bool JSDOMWindowBase::shouldInterruptScript(const JSGlobalObject* object)
{
    const JSDOMWindowBase* thisObject = static_cast<const JSDOMWindowBase*>(object);
    ASSERT(thisObject->wrapped().frame());
    Page* page = thisObject->wrapped().frame()->page();
    return shouldInterruptScriptToPreventInfiniteRecursionWhenClosingPage(page);
}

bool JSDOMWindowBase::shouldInterruptScriptBeforeTimeout(const JSGlobalObject* object)
{
    const JSDOMWindowBase* thisObject = static_cast<const JSDOMWindowBase*>(object);
    ASSERT(thisObject->wrapped().frame());
    Page* page = thisObject->wrapped().frame()->page();

    if (shouldInterruptScriptToPreventInfiniteRecursionWhenClosingPage(page))
        return true;

#if PLATFORM(IOS_FAMILY)
    if (page->chrome().client().isStopping())
        return true;
#endif

    return JSGlobalObject::shouldInterruptScriptBeforeTimeout(object);
}

RuntimeFlags JSDOMWindowBase::javaScriptRuntimeFlags(const JSGlobalObject* object)
{
    const JSDOMWindowBase* thisObject = static_cast<const JSDOMWindowBase*>(object);
    auto* frame = thisObject->wrapped().frame();
    if (!frame)
        return RuntimeFlags();
    return frame->settings().javaScriptRuntimeFlags();
}

class UserGestureInitiatedMicrotaskDispatcher final : public WebCoreMicrotaskDispatcher {
    WTF_MAKE_TZONE_ALLOCATED(UserGestureInitiatedMicrotaskDispatcher);
public:

    UserGestureInitiatedMicrotaskDispatcher(EventLoopTaskGroup& group, Ref<UserGestureToken>&& userGestureToken)
        : WebCoreMicrotaskDispatcher(Type::UserGestureIndicator, group)
        , m_userGestureToken(WTFMove(userGestureToken))
    {
    }

    ~UserGestureInitiatedMicrotaskDispatcher() final = default;

    JSC::QueuedTask::Result run(JSC::QueuedTask& task) final
    {
        auto runnability = currentRunnability();
        if (runnability == JSC::QueuedTask::Result::Executed) {
            UserGestureIndicator gestureIndicator(m_userGestureToken.ptr(), UserGestureToken::GestureScope::MediaOnly, UserGestureToken::ShouldPropagateToMicroTask::Yes);
            JSExecState::runTask(task.globalObject(), task);
        }
        return runnability;
    }

    static Ref<UserGestureInitiatedMicrotaskDispatcher> create(EventLoopTaskGroup& group, Ref<UserGestureToken>&& userGestureToken)
    {
        return adoptRef(*new UserGestureInitiatedMicrotaskDispatcher(group, WTFMove(userGestureToken)));
    }

private:
    const Ref<UserGestureToken> m_userGestureToken;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(UserGestureInitiatedMicrotaskDispatcher);


void JSDOMWindowBase::queueMicrotaskToEventLoop(JSGlobalObject& object, QueuedTask&& task)
{
    JSDOMWindowBase& thisObject = static_cast<JSDOMWindowBase&>(object);

    auto* objectScriptExecutionContext = thisObject.scriptExecutionContext();
    auto& eventLoop = objectScriptExecutionContext->eventLoop();
    // Propagating media only user gesture for Fetch API's promise chain.
    auto userGestureToken = UserGestureIndicator::currentUserGesture();
    if (userGestureToken && (!userGestureToken->shouldPropagateToMicroTask() || !objectScriptExecutionContext->settingsValues().userGesturePromisePropagationEnabled))
        userGestureToken = nullptr;

    if (!userGestureToken)
        task.setDispatcher(eventLoop.jsMicrotaskDispatcher());
    else
        task.setDispatcher(UserGestureInitiatedMicrotaskDispatcher::create(eventLoop, Ref { *userGestureToken }));

    eventLoop.queueMicrotask(WTFMove(task));
}

JSC::JSObject* JSDOMWindowBase::currentScriptExecutionOwner(JSGlobalObject* object)
{
    JSDOMWindowBase* thisObject = static_cast<JSDOMWindowBase*>(object);
    return jsCast<JSObject*>(toJS(thisObject, thisObject, thisObject->wrapped().documentIfLocal()));
}

JSC::ScriptExecutionStatus JSDOMWindowBase::scriptExecutionStatus(JSC::JSGlobalObject*, JSC::JSObject* owner)
{
    return jsCast<JSDocument*>(owner)->wrapped().jscScriptExecutionStatus();
}

void JSDOMWindowBase::reportViolationForUnsafeEval(JSGlobalObject* object, const String& source)
{
    const JSDOMWindowBase* thisObject = static_cast<const JSDOMWindowBase*>(object);
    CheckedPtr<ContentSecurityPolicy> contentSecurityPolicy;
    RefPtr localWindow = dynamicDowncast<LocalDOMWindow>(thisObject->wrapped());
    if (auto* element = localWindow ? localWindow->frameElement() : nullptr)
        contentSecurityPolicy = element->document().contentSecurityPolicy();

    if (!contentSecurityPolicy) {
        if (auto* document = localWindow ? localWindow->document() : nullptr)
            contentSecurityPolicy = document->contentSecurityPolicy();
    }

    if (!contentSecurityPolicy)
        return;

    contentSecurityPolicy->allowEval(object, LogToConsole::No, source);
}

void JSDOMWindowBase::willRemoveFromWindowProxy()
{
    setCurrentEvent(0);
}

void JSDOMWindowBase::setCurrentEvent(Event* currentEvent)
{
    m_currentEvent = currentEvent;
}

Event* JSDOMWindowBase::currentEvent() const
{
    return m_currentEvent.get();
}

JSWindowProxy& JSDOMWindowBase::proxy() const
{
    return *jsCast<JSWindowProxy*>(&JSDOMGlobalObject::proxy());
}

JSValue toJS(JSGlobalObject* lexicalGlobalObject, DOMWindow& domWindow)
{
    auto* frame = domWindow.frame();
    if (!frame)
        return jsNull();
    return toJS(lexicalGlobalObject, frame->windowProxy());
}

JSDOMWindow* toJSDOMWindow(LocalFrame& frame, DOMWrapperWorld& world)
{
    return JSC::jsCast<JSDOMWindow*>(frame.script().globalObject(world));
}

LocalDOMWindow& incumbentDOMWindow(JSGlobalObject& fallbackGlobalObject, CallFrame& callFrame)
{
    return downcast<LocalDOMWindow>(asJSDOMWindow(&callerGlobalObject(fallbackGlobalObject, &callFrame))->wrapped());
}

LocalDOMWindow& incumbentDOMWindow(JSGlobalObject& fallbackGlobalObject)
{
    return downcast<LocalDOMWindow>(asJSDOMWindow(&callerGlobalObject(fallbackGlobalObject, fallbackGlobalObject.vm().topCallFrame))->wrapped());
}

LocalDOMWindow& activeDOMWindow(JSGlobalObject& lexicalGlobalObject)
{
    return downcast<LocalDOMWindow>(asJSDOMWindow(&lexicalGlobalObject)->wrapped());
}

LocalDOMWindow& firstDOMWindow(JSGlobalObject& lexicalGlobalObject)
{
    VM& vm = lexicalGlobalObject.vm();
    return downcast<LocalDOMWindow>(asJSDOMWindow(vm.deprecatedVMEntryGlobalObject(&lexicalGlobalObject))->wrapped());
}

LocalDOMWindow& legacyActiveDOMWindowForAccessor(JSGlobalObject& fallbackGlobalObject, CallFrame& callFrame)
{
    return downcast<LocalDOMWindow>(asJSDOMWindow(&legacyActiveGlobalObjectForAccessor(fallbackGlobalObject, &callFrame))->wrapped());
}

LocalDOMWindow& legacyActiveDOMWindowForAccessor(JSGlobalObject& fallbackGlobalObject)
{
    return downcast<LocalDOMWindow>(asJSDOMWindow(&legacyActiveGlobalObjectForAccessor(fallbackGlobalObject, fallbackGlobalObject.vm().topCallFrame))->wrapped());
}

void JSDOMWindowBase::fireFrameClearedWatchpointsForWindow(LocalDOMWindow* window)
{
    JSC::VM& vm = commonVM();
    JSVMClientData* clientData = downcast<JSVMClientData>(vm.clientData);
    Vector<Ref<DOMWrapperWorld>> wrapperWorlds;
    clientData->getAllWorlds(wrapperWorlds);
    for (unsigned i = 0; i < wrapperWorlds.size(); ++i) {
        auto& wrappers = wrapperWorlds[i]->wrappers();
        auto result = wrappers.find(window);
        if (result == wrappers.end())
            continue;
        JSC::JSObject* wrapper = result->value.get();
        if (!wrapper)
            continue;
        JSDOMWindowBase* jsWindow = JSC::jsCast<JSDOMWindowBase*>(wrapper);
        jsWindow->m_windowCloseWatchpoints->fireAll(vm, "Frame cleared");
    }
}

} // namespace WebCore
