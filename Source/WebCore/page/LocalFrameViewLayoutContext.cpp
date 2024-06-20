/*
 * Copyright (C) 2017 Apple Inc. All Rights Reserved.
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

#include "config.h"
#include "LocalFrameViewLayoutContext.h"

#include "DebugPageOverlays.h"
#include "Document.h"
#include "InspectorInstrumentation.h"
#include "LayoutDisallowedScope.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "Quirks.h"
#include "RenderElement.h"
#include "RenderLayoutState.h"
#include "RenderStyleInlines.h"
#include "RenderView.h"
#include "ScriptDisallowedScope.h"
#include "Settings.h"
#include "StyleScope.h"
#include "LayoutBoxGeometry.h"
#include "LayoutContext.h"
#include "LayoutIntegrationLineLayout.h"
#include "LayoutState.h"
#include "LayoutTreeBuilder.h"
#include "RenderDescendantIterator.h"
#include "RenderStyleInlines.h"
#include <wtf/SetForScope.h>
#include <wtf/SystemTracing.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

UpdateScrollInfoAfterLayoutTransaction::UpdateScrollInfoAfterLayoutTransaction() = default;
UpdateScrollInfoAfterLayoutTransaction::~UpdateScrollInfoAfterLayoutTransaction() = default;

#ifndef NDEBUG
class RenderTreeNeedsLayoutChecker {
public :
    RenderTreeNeedsLayoutChecker(const RenderView& renderView)
        : m_renderView(renderView)
    {
    }

    ~RenderTreeNeedsLayoutChecker()
    {
        auto checkAndReportNeedsLayoutError = [] (const RenderObject& renderer) {
            auto needsLayout = [&] {
                if (renderer.needsLayout())
                    return true;
                if (auto* renderBlockFlow = dynamicDowncast<RenderBlockFlow>(renderer); renderBlockFlow && renderBlockFlow->modernLineLayout())
                    return renderBlockFlow->modernLineLayout()->hasDetachedContent();
                return false;
            };

            if (needsLayout()) {
                WTFReportError(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, "post-layout: dirty renderer(s)");
                renderer.showRenderTreeForThis();
                ASSERT_NOT_REACHED();
            }
        };

        checkAndReportNeedsLayoutError(m_renderView);

        for (auto* descendant = m_renderView.firstChild(); descendant; descendant = descendant->nextInPreOrder(&m_renderView))
            checkAndReportNeedsLayoutError(*descendant);
    }

private:
    const RenderView& m_renderView;
};
#endif

class LayoutScope {
public:
    LayoutScope(LocalFrameViewLayoutContext& layoutContext)
        : m_view(layoutContext.view())
        , m_nestedState(layoutContext.m_layoutNestedState, layoutContext.m_layoutNestedState == LocalFrameViewLayoutContext::LayoutNestedState::NotInLayout ? LocalFrameViewLayoutContext::LayoutNestedState::NotNested : LocalFrameViewLayoutContext::LayoutNestedState::Nested)
        , m_schedulingIsEnabled(layoutContext.m_layoutSchedulingIsEnabled, false)
        , m_previousScrollType(layoutContext.view().currentScrollType())
    {
        m_view.setCurrentScrollType(ScrollType::Programmatic);
    }
        
    ~LayoutScope()
    {
        m_view.setCurrentScrollType(m_previousScrollType);
    }
        
private:
    LocalFrameView& m_view;
    SetForScope<LocalFrameViewLayoutContext::LayoutNestedState> m_nestedState;
    SetForScope<bool> m_schedulingIsEnabled;
    ScrollType m_previousScrollType;
};

LocalFrameViewLayoutContext::LocalFrameViewLayoutContext(LocalFrameView& frameView)
    : m_frameView(frameView)
    , m_layoutTimer(*this, &LocalFrameViewLayoutContext::layoutTimerFired)
    , m_postLayoutTaskTimer(*this, &LocalFrameViewLayoutContext::runPostLayoutTasks)
{
}

LocalFrameViewLayoutContext::~LocalFrameViewLayoutContext()
{
}

UpdateScrollInfoAfterLayoutTransaction& LocalFrameViewLayoutContext::updateScrollInfoAfterLayoutTransaction()
{
    if (!m_updateScrollInfoAfterLayoutTransaction)
        m_updateScrollInfoAfterLayoutTransaction = makeUnique<UpdateScrollInfoAfterLayoutTransaction>();
    return *m_updateScrollInfoAfterLayoutTransaction;
}

void LocalFrameViewLayoutContext::layout()
{
    LOG_WITH_STREAM(Layout, stream << "LocalFrameView " << &view() << " LocalFrameViewLayoutContext::layout() with size " << view().layoutSize());

    Ref protectedView(view());

    performLayout();

    if (view().hasOneRef())
        return;

    Style::Scope::QueryContainerUpdateContext queryContainerUpdateContext;
    while (document() && document()->styleScope().updateQueryContainerState(queryContainerUpdateContext)) {
        document()->updateStyleIfNeeded();

        if (!needsLayout())
            break;

        performLayout();

        if (view().hasOneRef())
            return;
    }
}

void LocalFrameViewLayoutContext::performLayout()
{
    Ref frame = this->frame();
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(!document()->inRenderTreeUpdate());
    ASSERT(LayoutDisallowedScope::isLayoutAllowed());
    ASSERT(!view().isPainting());
    ASSERT(frame->view() == &view());
    ASSERT(document());
    ASSERT(document()->backForwardCacheState() == Document::NotInBackForwardCache
        || document()->backForwardCacheState() == Document::AboutToEnterBackForwardCache);
    if (!canPerformLayout()) {
        LOG(Layout, "  is not allowed, bailing");
        return;
    }

    LayoutScope layoutScope(*this);
    TraceScope tracingScope(PerformLayoutStart, PerformLayoutEnd);
    ScriptDisallowedScope::InMainThread scriptDisallowedScope;
    InspectorInstrumentation::willLayout(frame);
    RenderElement* subtreeLayoutRoot = layoutType() == LayoutType::SubtreeLayout ? m_subtreeLayoutRoots.takeAny() : nullptr;
    SingleThreadWeakPtr<RenderElement> layoutRoot;
    
    m_layoutTimer.stop();
    m_setNeedsLayoutWasDeferred = false;

#if !LOG_DISABLED
    if (m_firstLayout && !frame->ownerElement())
        LOG_WITH_STREAM(Layout, stream << "LocalFrameView " << &view() << " elapsed time before first layout: " << document()->timeSinceDocumentCreation());
#endif
#if PLATFORM(IOS_FAMILY)
    if (protectedView()->updateFixedPositionLayoutRect() && layoutType() == LayoutType::SubtreeLayout)
        convertSubtreeLayoutToFullLayout();
#endif
    {
        SetForScope layoutPhase(m_layoutPhase, LayoutPhase::InPreLayout);

        if (!document()->isResolvingContainerQueriesForSelfOrAncestor()) {
            // If this is a new top-level layout and there are any remaining tasks from the previous layout, finish them now.
            if (!isLayoutNested() && m_postLayoutTaskTimer.isActive())
                runPostLayoutTasks();

            updateStyleForLayout();
        }

        if (view().hasOneRef())
            return;

        protectedView()->autoSizeIfEnabled();
        if (!renderView())
            return;

        layoutRoot = subtreeLayoutRoot ?: renderView();
        m_needsFullRepaint = is<RenderView>(layoutRoot) && (m_firstLayout || renderView()->printing());

        LOG_WITH_STREAM(Layout, stream << "LocalFrameView " << &view() << " layout " << m_layoutCount << " - subtree root " << subtreeLayoutRoot << ", needsFullRepaint " << m_needsFullRepaint);

        protectedView()->willDoLayout(layoutRoot);
        m_firstLayout = false;
    }

    auto isSimplifiedLayout = layoutRoot->needsSimplifiedNormalFlowLayoutOnly();
    {
        TraceScope tracingScope(RenderTreeLayoutStart, RenderTreeLayoutEnd);
        SetForScope layoutPhase(m_layoutPhase, LayoutPhase::InRenderTreeLayout);
        ScriptDisallowedScope::InMainThread scriptDisallowedScope;
        SubtreeLayoutStateMaintainer subtreeLayoutStateMaintainer(subtreeLayoutRoot);
        RenderView::RepaintRegionAccumulator repaintRegionAccumulator(renderView());
#ifndef NDEBUG
        RenderTreeNeedsLayoutChecker checker(*renderView());
#endif
        ++m_layoutIdentifier;
        layoutRoot->layout();
        ++m_layoutCount;
#if ENABLE(TEXT_AUTOSIZING)
        applyTextSizingIfNeeded(*layoutRoot.get());
#endif
        if (subtreeLayoutRoot) {
            removeSubtreeLayoutRoot(*subtreeLayoutRoot);

            // When performing a subtree layout, we may need to perform layout for each subtree.
            // The above might have been done in single pass, but calling performLayout() multiple times
            // has some nice side-effects such as reporting "Layout" events in the inspector with specific subtree areas.
            if (m_subtreeLayoutRoots.size() > 0) {
                // Please note that due to possibility of performLayout() failing as well as recursive calls, we need to loop
                // and track the progress at the same time not to end up in infinite loop.
                // Also, please note that such extra calls to performLayout() must all happen before RenderTreeNeedsLayoutChecker
                // goes out of scope, so that the state is clean after all the calls.
                unsigned numberOfSubtreesBeforeLayout, numberOfSubtreesAfterLayout;
                do {
                    numberOfSubtreesBeforeLayout = m_subtreeLayoutRoots.size();
                    performLayout();
                    numberOfSubtreesAfterLayout = m_subtreeLayoutRoots.size();
                } while (numberOfSubtreesAfterLayout > 0 && numberOfSubtreesAfterLayout < numberOfSubtreesBeforeLayout);
            }
        }

#if !LOG_DISABLED && ENABLE(TREE_DEBUGGING)
        auto layoutLogEnabled = [] {
            return LogLayout.state == WTFLogChannelState::On;
        };
        if (layoutLogEnabled())
            showRenderTree(renderView());
#endif
    }
    {
        SetForScope layoutPhase(m_layoutPhase, LayoutPhase::InViewSizeAdjust);
        ScriptDisallowedScope::InMainThread scriptDisallowedScope;
        if (is<RenderView>(layoutRoot) && !renderView()->printing()) {
            // This is to protect m_needsFullRepaint's value when layout() is getting re-entered through adjustViewSize().
            SetForScope needsFullRepaint(m_needsFullRepaint);
            protectedView()->adjustViewSize();
            // FIXME: Firing media query callbacks synchronously on nested frames could produced a detached FrameView here by
            // navigating away from the current document (see webkit.org/b/173329).
            if (view().hasOneRef())
                return;
        }
    }
    {
        SetForScope layoutPhase(m_layoutPhase, LayoutPhase::InPostLayout);
        if (m_needsFullRepaint)
            renderView()->repaintRootContents();
        ASSERT(!layoutRoot->needsLayout());
        protectedView()->didLayout(layoutRoot, isSimplifiedLayout);
        runOrScheduleAsynchronousTasks();
    }
    InspectorInstrumentation::didLayout(frame, *layoutRoot);
    DebugPageOverlays::didLayout(frame);
}

void LocalFrameViewLayoutContext::runOrScheduleAsynchronousTasks()
{
    if (m_postLayoutTaskTimer.isActive())
        return;

    if (document()->isResolvingContainerQueries()) {
        // We are doing layout from style resolution to resolve container queries.
        m_postLayoutTaskTimer.startOneShot(0_s);
        return;
    }

    // If we are already in performPostLayoutTasks(), defer post layout tasks until after we return
    // to avoid re-entrancy.
    if (m_inAsynchronousTasks) {
        m_postLayoutTaskTimer.startOneShot(0_s);
        return;
    }

    runPostLayoutTasks();
    if (needsLayout()) {
        // If runPostLayoutTasks() made us layout again, let's defer the tasks until after we return.
        m_postLayoutTaskTimer.startOneShot(0_s);
        layout();
    }
}

void LocalFrameViewLayoutContext::runPostLayoutTasks()
{
    m_postLayoutTaskTimer.stop();
    if (m_inAsynchronousTasks)
        return;
    SetForScope inAsynchronousTasks(m_inAsynchronousTasks, true);
    protectedView()->performPostLayoutTasks();
}

void LocalFrameViewLayoutContext::flushPostLayoutTasks()
{
    if (!m_postLayoutTaskTimer.isActive())
        return;
    runPostLayoutTasks();
}

void LocalFrameViewLayoutContext::reset()
{
    m_layoutPhase = LayoutPhase::OutsideLayout;
    clearSubtreeLayoutRoots();
    m_layoutCount = 0;
    m_layoutSchedulingIsEnabled = true;
    m_layoutTimer.stop();
    m_firstLayout = true;
    m_postLayoutTaskTimer.stop();
    m_needsFullRepaint = true;
}

bool LocalFrameViewLayoutContext::needsLayout() const
{
    // This can return true in cases where the document does not have a body yet.
    // Document::shouldScheduleLayout takes care of preventing us from scheduling
    // layout in that case.
    return isLayoutPending()
        || layoutType() != LayoutType::NoLayout
        || (m_disableSetNeedsLayoutCount && m_setNeedsLayoutWasDeferred);
}

void LocalFrameViewLayoutContext::setNeedsLayoutAfterViewConfigurationChange()
{
    if (m_disableSetNeedsLayoutCount) {
        m_setNeedsLayoutWasDeferred = true;
        return;
    }

    if (auto* renderView = this->renderView()) {
        ASSERT(!document()->inHitTesting());
        renderView->setNeedsLayout();
        scheduleLayout();
    }
}

void LocalFrameViewLayoutContext::enableSetNeedsLayout()
{
    ASSERT(m_disableSetNeedsLayoutCount);
    if (!--m_disableSetNeedsLayoutCount)
        m_setNeedsLayoutWasDeferred = false; // FIXME: Find a way to make the deferred layout actually happen.
}

void LocalFrameViewLayoutContext::disableSetNeedsLayout()
{
    ++m_disableSetNeedsLayoutCount;
}

void LocalFrameViewLayoutContext::scheduleLayout()
{
    // FIXME: We should assert the page is not in the back/forward cache, but that is causing
    // too many false assertions. See <rdar://problem/7218118>.
    ASSERT(frame().view() == &view());
    if (!document())
        return;

    if (layoutType() == LayoutType::SubtreeLayout)
        convertSubtreeLayoutToFullLayout();

    if (isLayoutPending())
        return;

    if (!isLayoutSchedulingEnabled() || !document()->shouldScheduleLayout())
        return;
    if (!needsLayout())
        return;

#if !LOG_DISABLED
    if (!document()->ownerElement())
        LOG(Layout, "LocalFrameView %p layout timer scheduled at %.3fs", this, document()->timeSinceDocumentCreation().value());
#endif

    InspectorInstrumentation::didInvalidateLayout(protectedFrame());
    m_layoutTimer.startOneShot(0_s);
}

void LocalFrameViewLayoutContext::unscheduleLayout()
{
    if (m_postLayoutTaskTimer.isActive())
        m_postLayoutTaskTimer.stop();

    if (!m_layoutTimer.isActive())
        return;

#if !LOG_DISABLED
    if (!document()->ownerElement())
        LOG_WITH_STREAM(Layout, stream << "LocalFrameViewLayoutContext for LocalFrameView " << frame().view() << " layout timer unscheduled at " << document()->timeSinceDocumentCreation().value());
#endif

    m_layoutTimer.stop();
}

void LocalFrameViewLayoutContext::scheduleSubtreeLayout(RenderElement& layoutRoot)
{
    ASSERT(renderView());
    auto& renderView = *this->renderView();

    // Try to catch unnecessary work during render tree teardown.
    ASSERT(!renderView.renderTreeBeingDestroyed());
    ASSERT(frame().view() == &view());

    if (layoutType() == LayoutType::FullLayout) {
        // We already have a pending (full) layout. Just mark the subtree for layout.
        layoutRoot.markContainingBlocksForLayout(&renderView);
        return;
    }

    if (!isLayoutPending() && isLayoutSchedulingEnabled()) {
        ASSERT(!layoutRoot.container() || is<RenderView>(layoutRoot.container()) || !layoutRoot.container()->needsLayout());
        addSubtreeLayoutRoot(layoutRoot);
        InspectorInstrumentation::didInvalidateLayout(protectedFrame());
        m_layoutTimer.startOneShot(0_s);
        return;
    }

    if (hasSubtreeLayoutRoot(layoutRoot))
        return;

    if (layoutType() != LayoutType::SubtreeLayout) {
        // We don't have any pending layout and we can't schedule subtree layout. Mark subtree and issue a full layout.
        layoutRoot.markContainingBlocksForLayout(&renderView);
        InspectorInstrumentation::didInvalidateLayout(protectedFrame());
        return;
    }

    // FIXME: Check if combining subtrees being ancestors with each other makes sense from performance POV.

    // We already have a pending subtree layout. Just add new subtree to collection.
    addSubtreeLayoutRoot(layoutRoot);
}

LocalFrameViewLayoutContext::LayoutType LocalFrameViewLayoutContext::layoutType() const
{
    if (!m_subtreeLayoutRoots.isEmpty())
        return LayoutType::SubtreeLayout;
    if (auto* renderView = this->renderView(); renderView && renderView->needsLayout())
        return LayoutType::FullLayout;
    return LayoutType::NoLayout;
}

void LocalFrameViewLayoutContext::layoutTimerFired()
{
#if !LOG_DISABLED
    if (!document()->ownerElement())
        LOG_WITH_STREAM(Layout, stream << "LocalFrameViewLayoutContext for LocalFrameView " << frame().view() << " layout timer fired at " << document()->timeSinceDocumentCreation().value());
#endif
    layout();
}

bool LocalFrameViewLayoutContext::hasSubtreeLayoutRoot(const RenderElement& subtreeLayoutRoot) const
{
    return m_subtreeLayoutRoots.contains(const_cast<RenderElement*>(&subtreeLayoutRoot));
}

void LocalFrameViewLayoutContext::clearSubtreeLayoutRoots()
{
    m_subtreeLayoutRoots.clear();
}

void LocalFrameViewLayoutContext::convertSubtreeLayoutToFullLayout()
{
    ASSERT(!m_subtreeLayoutRoots.isEmpty());
    for (auto* subtreeLayoutRoot : m_subtreeLayoutRoots)
        subtreeLayoutRoot->markContainingBlocksForLayout(renderView());
    clearSubtreeLayoutRoots();
}

void LocalFrameViewLayoutContext::addSubtreeLayoutRoot(RenderElement& subtreeLayoutRoot)
{
    m_subtreeLayoutRoots.add(&subtreeLayoutRoot);
}

void LocalFrameViewLayoutContext::removeSubtreeLayoutRoot(const RenderElement& subtreeLayoutRoot)
{
    m_subtreeLayoutRoots.remove(const_cast<RenderElement*>(&subtreeLayoutRoot));
}

bool LocalFrameViewLayoutContext::canPerformLayout() const
{
    if (isInRenderTreeLayout())
        return false;

    if (view().isPainting())
        return false;

    if (m_subtreeLayoutRoots.isEmpty() && !document()->renderView())
        return false;

    return true;
}

#if ENABLE(TEXT_AUTOSIZING)
void LocalFrameViewLayoutContext::applyTextSizingIfNeeded(RenderElement& layoutRoot)
{
    ASSERT(document());
    if (document()->quirks().shouldIgnoreTextAutoSizing())
        return;
    auto& settings = layoutRoot.settings();
    bool idempotentMode = settings.textAutosizingUsesIdempotentMode();
    if (!settings.textAutosizingEnabled() || idempotentMode || renderView()->printing())
        return;
    auto minimumZoomFontSize = settings.minimumZoomFontSize();
    if (!idempotentMode && !minimumZoomFontSize)
        return;
    auto textAutosizingWidth = layoutRoot.page().textAutosizingWidth();
    if (auto overrideWidth = settings.textAutosizingWindowSizeOverrideWidth())
        textAutosizingWidth = overrideWidth;
    if (!idempotentMode && !textAutosizingWidth)
        return;
    layoutRoot.adjustComputedFontSizesOnBlocks(minimumZoomFontSize, textAutosizingWidth);
    if (!layoutRoot.needsLayout())
        return;
    LOG(TextAutosizing, "Text Autosizing: minimumZoomFontSize=%.2f textAutosizingWidth=%.2f", minimumZoomFontSize, textAutosizingWidth);
    layoutRoot.layout();
}
#endif

void LocalFrameViewLayoutContext::updateStyleForLayout()
{
    ScriptDisallowedScope::InMainThread scriptDisallowedScope;
    Ref document = *this->document();

    // FIXME: This shouldn't be necessary, but see rdar://problem/36670246.
    if (!document->styleScope().resolverIfExists())
        document->styleScope().didChangeStyleSheetEnvironment();

    // Viewport-dependent media queries may cause us to need completely different style information.
    document->styleScope().evaluateMediaQueriesForViewportChange();

    document->updateElementsAffectedByMediaQueries();
    // If there is any pagination to apply, it will affect the RenderView's style, so we should
    // take care of that now.
    protectedView()->applyPaginationToViewport();
    // Always ensure our style info is up-to-date. This can happen in situations where
    // the layout beats any sort of style recalc update that needs to occur.
    document->updateStyleIfNeeded();
}

LayoutSize LocalFrameViewLayoutContext::layoutDelta() const
{
    if (auto* layoutState = this->layoutState())
        return layoutState->layoutDelta();
    return { };
}

void LocalFrameViewLayoutContext::addLayoutDelta(const LayoutSize& delta)
{
    if (auto* layoutState = this->layoutState())
        layoutState->addLayoutDelta(delta);
}
    
#if ASSERT_ENABLED
bool LocalFrameViewLayoutContext::layoutDeltaMatches(const LayoutSize& delta)
{
    if (auto* layoutState = this->layoutState())
        return layoutState->layoutDeltaMatches(delta);
    return false;
}
#endif

RenderLayoutState* LocalFrameViewLayoutContext::layoutState() const
{
    if (m_layoutStateStack.isEmpty())
        return nullptr;
    return m_layoutStateStack.last().get();
}

void LocalFrameViewLayoutContext::pushLayoutState(RenderElement& root)
{
    ASSERT(!m_paintOffsetCacheDisableCount);
    ASSERT(!layoutState());

    m_layoutStateStack.append(makeUnique<RenderLayoutState>(root));
}

bool LocalFrameViewLayoutContext::pushLayoutState(RenderBox& renderer, const LayoutSize& offset, LayoutUnit pageHeight, bool pageHeightChanged)
{
    // We push LayoutState even if layoutState is disabled because it stores layoutDelta too.
    auto* layoutState = this->layoutState();
    if (!layoutState || !needsFullRepaint() || layoutState->isPaginated() || renderer.enclosingFragmentedFlow()
        || layoutState->lineGrid() || (renderer.style().lineGrid() != RenderStyle::initialLineGrid() && renderer.isRenderBlockFlow())) {
        m_layoutStateStack.append(makeUnique<RenderLayoutState>(m_layoutStateStack
            , renderer
            , offset
            , pageHeight
            , pageHeightChanged
            , layoutState ? layoutState->lineClamp() : std::nullopt
            , layoutState ? layoutState->textBoxTrim() : RenderLayoutState::TextBoxTrim()));
        return true;
    }
    return false;
}
    
void LocalFrameViewLayoutContext::popLayoutState()
{
    if (!layoutState())
        return;

    auto currentLineClamp = layoutState()->lineClamp();

    m_layoutStateStack.removeLast();

    if (currentLineClamp) {
        // Propagates the current line clamp state to the parent.
        if (auto* layoutState = this->layoutState(); layoutState && layoutState->lineClamp()) {
            ASSERT(layoutState->lineClamp()->maximumLineCount == currentLineClamp->maximumLineCount);
            layoutState->setLineClamp(currentLineClamp);
        }
    }
}

void LocalFrameViewLayoutContext::setBoxNeedsTransformUpdateAfterContainerLayout(RenderBox& box, RenderBlock& container)
{
    auto it = m_containersWithDescendantsNeedingTransformUpdate.ensure(container, [] { return Vector<SingleThreadWeakPtr<RenderBox>>({ }); });
    it.iterator->value.append(WeakPtr { box });
}

Vector<SingleThreadWeakPtr<RenderBox>> LocalFrameViewLayoutContext::takeBoxesNeedingTransformUpdateAfterContainerLayout(RenderBlock& container)
{
    return m_containersWithDescendantsNeedingTransformUpdate.take(container);
}

#ifndef NDEBUG
void LocalFrameViewLayoutContext::checkLayoutState()
{
    ASSERT(layoutDeltaMatches(LayoutSize()));
    ASSERT(!m_paintOffsetCacheDisableCount);
}
#endif

LocalFrame& LocalFrameViewLayoutContext::frame() const
{
    return view().frame();
}

Ref<LocalFrame> LocalFrameViewLayoutContext::protectedFrame()
{
    return frame();
}

LocalFrameView& LocalFrameViewLayoutContext::view() const
{
    return m_frameView.get();
}

Ref<LocalFrameView> LocalFrameViewLayoutContext::protectedView() const
{
    return m_frameView.get();
}

RenderView* LocalFrameViewLayoutContext::renderView() const
{
    return view().renderView();
}

Document* LocalFrameViewLayoutContext::document() const
{
    return frame().document();
}

} // namespace WebCore
