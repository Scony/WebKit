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
#include "PositionArea.h"

#include <wtf/text/TextStream.h>

namespace WebCore {

bool PositionArea::isAmbiguous(const CSSValueID id) {
    return (
        id == CSSValueCenter
        || id == CSSValueStart
        || id == CSSValueEnd
        || id == CSSValueSpanStart
        || id == CSSValueSpanEnd
        || id == CSSValueSelfStart
        || id == CSSValueSelfEnd
        || id == CSSValueSpanSelfStart
        || id == CSSValueSpanSelfEnd
        || id == CSSValueSpanAll
    );
}

static TextStream& operator<<(TextStream& ts, const PositionArea::Cols& positionAreaCols)
{
    switch (positionAreaCols) {
    case PositionArea::Cols::Center:
        return ts << "Center";
    case PositionArea::Cols::End:
        return ts << "End";
    case PositionArea::Cols::InlineEnd:
        return ts << "InlineEnd";
    case PositionArea::Cols::InlineStart:
        return ts << "InlineStart";
    case PositionArea::Cols::Left:
        return ts << "Left";
    case PositionArea::Cols::Right:
        return ts << "Right";
    case PositionArea::Cols::SelfEnd:
        return ts << "SelfEnd";
    case PositionArea::Cols::SelfInlineEnd:
        return ts << "SelfInlineEnd";
    case PositionArea::Cols::SelfInlineStart:
        return ts << "SelfInlineStart";
    case PositionArea::Cols::SelfStart:
        return ts << "SelfStart";
    case PositionArea::Cols::SpanAll:
        return ts << "SpanAll";
    case PositionArea::Cols::SpanEnd:
        return ts << "SpanEnd";
    case PositionArea::Cols::SpanInlineEnd:
        return ts << "SpanInlineEnd";
    case PositionArea::Cols::SpanInlineStart:
        return ts << "SpanInlineStart";
    case PositionArea::Cols::SpanLeft:
        return ts << "SpanLeft";
    case PositionArea::Cols::SpanRight:
        return ts << "SpanRight";
    case PositionArea::Cols::SpanSelfEnd:
        return ts << "SpanSelfEnd";
    case PositionArea::Cols::SpanSelfInlineEnd:
        return ts << "SpanSelfInlineEnd";
    case PositionArea::Cols::SpanSelfInlineStart:
        return ts << "SpanSelfInlineStart";
    case PositionArea::Cols::SpanSelfStart:
        return ts << "SpanSelfStart";
    case PositionArea::Cols::SpanStart:
        return ts << "SpanStart";
    case PositionArea::Cols::SpanXEnd:
        return ts << "SpanXEnd";
    case PositionArea::Cols::SpanXSelfEnd:
        return ts << "SpanXSelfEnd";
    case PositionArea::Cols::SpanXSelfStart:
        return ts << "SpanXSelfStart";
    case PositionArea::Cols::SpanXStart:
        return ts << "SpanXStart";
    case PositionArea::Cols::Start:
        return ts << "Start";
    case PositionArea::Cols::XEnd:
        return ts << "XEnd";
    case PositionArea::Cols::XSelfEnd:
        return ts << "XSelfEnd";
    case PositionArea::Cols::XSelfStart:
        return ts << "XSelfStart";
    case PositionArea::Cols::XStart:
        return ts << "XStart";
    }
    ASSERT_NOT_REACHED();
    return ts;
}

static TextStream& operator<<(TextStream& ts, const PositionArea::Rows& positionAreaRows)
{
    switch (positionAreaRows) {
    case PositionArea::Rows::BlockEnd:
        return ts << "BlockEnd";
    case PositionArea::Rows::BlockStart:
        return ts << "BlockStart";
    case PositionArea::Rows::Bottom:
        return ts << "Bottom";
    case PositionArea::Rows::Center:
        return ts << "Center";
    case PositionArea::Rows::End:
        return ts << "End";
    case PositionArea::Rows::SelfBlockEnd:
        return ts << "SelfBlockEnd";
    case PositionArea::Rows::SelfBlockStart:
        return ts << "SelfBlockStart";
    case PositionArea::Rows::SelfEnd:
        return ts << "SelfEnd";
    case PositionArea::Rows::SelfStart:
        return ts << "SelfStart";
    case PositionArea::Rows::SpanAll:
        return ts << "SpanAll";
    case PositionArea::Rows::SpanBlockEnd:
        return ts << "SpanBlockEnd";
    case PositionArea::Rows::SpanBlockStart:
        return ts << "SpanBlockStart";
    case PositionArea::Rows::SpanBottom:
        return ts << "SpanBottom";
    case PositionArea::Rows::SpanEnd:
        return ts << "SpanEnd";
    case PositionArea::Rows::SpanSelfBlockEnd:
        return ts << "SpanSelfBlockEnd";
    case PositionArea::Rows::SpanSelfBlockStart:
        return ts << "SpanSelfBlockStart";
    case PositionArea::Rows::SpanSelfEnd:
        return ts << "SpanSelfEnd";
    case PositionArea::Rows::SpanSelfStart:
        return ts << "SpanSelfStart";
    case PositionArea::Rows::SpanStart:
        return ts << "SpanStart";
    case PositionArea::Rows::SpanTop:
        return ts << "SpanTop";
    case PositionArea::Rows::SpanYEnd:
        return ts << "SpanYEnd";
    case PositionArea::Rows::SpanYSelfEnd:
        return ts << "SpanYSelfEnd";
    case PositionArea::Rows::SpanYSelfStart:
        return ts << "SpanYSelfStart";
    case PositionArea::Rows::SpanYStart:
        return ts << "SpanYStart";
    case PositionArea::Rows::Start:
        return ts << "Start";
    case PositionArea::Rows::Top:
        return ts << "Top";
    case PositionArea::Rows::YEnd:
        return ts << "YEnd";
    case PositionArea::Rows::YSelfEnd:
        return ts << "YSelfEnd";
    case PositionArea::Rows::YSelfStart:
        return ts << "YSelfStart";
    case PositionArea::Rows::YStart:
        return ts << "YStart";
    }
    ASSERT_NOT_REACHED();
    return ts;
}

TextStream& operator<<(TextStream& ts, const PositionArea& postionArea)
{
    return ts << postionArea.cols << ' ' << postionArea.rows;
}

} // namespace WebCore
