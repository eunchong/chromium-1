/*
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2009, 2010, 2011 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef InlineBox_h
#define InlineBox_h

#include "core/layout/api/LineLayoutBoxModel.h"
#include "core/layout/api/LineLayoutItem.h"
#include "core/layout/api/SelectionState.h"
#include "platform/graphics/paint/DisplayItemClient.h"
#include "platform/text/TextDirection.h"

namespace blink {

class HitTestRequest;
class HitTestResult;
class LayoutObject;
class RootInlineBox;

enum MarkLineBoxes { MarkLineBoxesDirty, DontMarkLineBoxes };

// InlineBox represents a rectangle that occurs on a line.  It corresponds to
// some LayoutObject (i.e., it represents a portion of that LayoutObject).
class InlineBox : public DisplayItemClient {
    WTF_MAKE_NONCOPYABLE(InlineBox);
public:
    InlineBox(LineLayoutItem obj)
        : m_next(nullptr)
        , m_prev(nullptr)
        , m_parent(nullptr)
        , m_lineLayoutItem(obj)
        , m_logicalWidth()
#if ENABLE(ASSERT)
        , m_hasBadParent(false)
#endif
    {
    }

    InlineBox(LineLayoutItem item, LayoutPoint topLeft, LayoutUnit logicalWidth, bool firstLine, bool constructed,
        bool dirty, bool extracted, bool isHorizontal, InlineBox* next, InlineBox* prev, InlineFlowBox* parent)
        : m_next(next)
        , m_prev(prev)
        , m_parent(parent)
        , m_lineLayoutItem(item)
        , m_topLeft(topLeft)
        , m_logicalWidth(logicalWidth)
        , m_bitfields(firstLine, constructed, dirty, extracted, isHorizontal)
#if ENABLE(ASSERT)
        , m_hasBadParent(false)
#endif
    {
    }

    virtual ~InlineBox();

    virtual void destroy();

    virtual void deleteLine();
    virtual void extractLine();
    virtual void attachLine();

    virtual bool isLineBreak() const { return false; }

    // These methods are called when the caller wants to move the position of InlineBox without full layout of it.
    // The implementation should update the position of the whole subtree (e.g. position of descendants and overflow etc.
    // should also be moved accordingly).
    virtual void move(const LayoutSize& delta);
    void moveInLogicalDirection(const LayoutSize& deltaInLogicalDirection) { move(isHorizontal() ? deltaInLogicalDirection : deltaInLogicalDirection.transposedSize()); }
    void moveInInlineDirection(LayoutUnit delta) { moveInLogicalDirection(LayoutSize(delta, LayoutUnit())); }
    void moveInBlockDirection(LayoutUnit delta) { moveInLogicalDirection(LayoutSize(LayoutUnit(), delta)); }

    virtual void paint(const PaintInfo&, const LayoutPoint&, LayoutUnit lineTop, LayoutUnit lineBottom) const;
    virtual bool nodeAtPoint(HitTestResult&, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, LayoutUnit lineTop, LayoutUnit lineBottom);

    // InlineBoxes are allocated out of the rendering partition.
    void* operator new(size_t);
    void operator delete(void*);

#ifndef NDEBUG
    void showTreeForThis() const;
    void showLineTreeForThis() const;

    virtual void showBox(int = 0) const;
    virtual void showLineTreeAndMark(const InlineBox* = nullptr, const char* = nullptr, const InlineBox* = nullptr, const char* = nullptr, const LayoutObject* = nullptr, int = 0) const;
#endif

    virtual const char* boxName() const;

    // DisplayItemClient methods
    String debugName() const override;
    LayoutRect visualRect() const override;

    bool isText() const { return m_bitfields.isText(); }
    void setIsText(bool isText) { m_bitfields.setIsText(isText); }

    virtual bool isInlineFlowBox() const { return false; }
    virtual bool isInlineTextBox() const { return false; }
    virtual bool isRootInlineBox() const { return false; }

    virtual bool isSVGInlineTextBox() const { return false; }
    virtual bool isSVGInlineFlowBox() const { return false; }
    virtual bool isSVGRootInlineBox() const { return false; }

    bool hasVirtualLogicalHeight() const { return m_bitfields.hasVirtualLogicalHeight(); }
    void setHasVirtualLogicalHeight() { m_bitfields.setHasVirtualLogicalHeight(true); }
    virtual LayoutUnit virtualLogicalHeight() const
    {
        ASSERT_NOT_REACHED();
        return LayoutUnit();
    }

    bool isHorizontal() const { return m_bitfields.isHorizontal(); }
    void setIsHorizontal(bool isHorizontal) { m_bitfields.setIsHorizontal(isHorizontal); }

    virtual LayoutRect calculateBoundaries() const
    {
        ASSERT_NOT_REACHED();
        return LayoutRect();
    }

    bool isConstructed() { return m_bitfields.constructed(); }
    virtual void setConstructed() { m_bitfields.setConstructed(true); }

    void setExtracted(bool extracted = true) { m_bitfields.setExtracted(extracted); }

    void setFirstLineStyleBit(bool firstLine) { m_bitfields.setFirstLine(firstLine); }
    bool isFirstLineStyle() const { return m_bitfields.firstLine(); }

    void remove(MarkLineBoxes = MarkLineBoxesDirty);

    InlineBox* nextOnLine() const { return m_next; }
    InlineBox* prevOnLine() const { return m_prev; }
    void setNextOnLine(InlineBox* next)
    {
        ASSERT(m_parent || !next);
        m_next = next;
    }
    void setPrevOnLine(InlineBox* prev)
    {
        ASSERT(m_parent || !prev);
        m_prev = prev;
    }
    bool nextOnLineExists() const;

    virtual bool isLeaf() const { return true; }

    InlineBox* nextLeafChild() const;
    InlineBox* prevLeafChild() const;

    // Helper functions for editing and hit-testing code.
    // FIXME: These two functions should be moved to RenderedPosition once the code to convert between
    // Position and inline box, offset pair is moved to RenderedPosition.
    InlineBox* nextLeafChildIgnoringLineBreak() const;
    InlineBox* prevLeafChildIgnoringLineBreak() const;

    LineLayoutItem getLineLayoutItem() const { return m_lineLayoutItem; }

    InlineFlowBox* parent() const
    {
        ASSERT(!m_hasBadParent);
        return m_parent;
    }
    void setParent(InlineFlowBox* par) { m_parent = par; }

    const RootInlineBox& root() const;
    RootInlineBox& root();

    // x() is the left side of the box in the containing block's coordinate system.
    void setX(LayoutUnit x) { m_topLeft.setX(x); }
    LayoutUnit x() const { return m_topLeft.x(); }
    LayoutUnit left() const { return m_topLeft.x(); }

    // y() is the top side of the box in the containing block's coordinate system.
    void setY(LayoutUnit y) { m_topLeft.setY(y); }
    LayoutUnit y() const { return m_topLeft.y(); }
    LayoutUnit top() const { return m_topLeft.y(); }

    const LayoutPoint& topLeft() const { return m_topLeft; }

    LayoutUnit width() const { return isHorizontal() ? logicalWidth() : logicalHeight(); }
    LayoutUnit height() const { return isHorizontal() ? logicalHeight() : logicalWidth(); }
    LayoutSize size() const { return LayoutSize(width(), height()); }
    LayoutUnit right() const { return left() + width(); }
    LayoutUnit bottom() const { return top() + height(); }

    // The logicalLeft position is the left edge of the line box in a horizontal line and the top edge in a vertical line.
    LayoutUnit logicalLeft() const { return isHorizontal() ? m_topLeft.x() : m_topLeft.y(); }
    LayoutUnit logicalRight() const { return logicalLeft() + logicalWidth(); }
    void setLogicalLeft(LayoutUnit left)
    {
        if (isHorizontal())
            setX(left);
        else
            setY(left);
    }
    int pixelSnappedLogicalLeft() const { return logicalLeft(); }
    int pixelSnappedLogicalRight() const { return logicalRight().ceil(); }
    int pixelSnappedLogicalTop() const { return logicalTop(); }
    int pixelSnappedLogicalBottom() const { return logicalBottom().ceil(); }

    // The logicalTop[ position is the top edge of the line box in a horizontal line and the left edge in a vertical line.
    LayoutUnit logicalTop() const { return isHorizontal() ? m_topLeft.y() : m_topLeft.x(); }
    LayoutUnit logicalBottom() const { return logicalTop() + logicalHeight(); }
    void setLogicalTop(LayoutUnit top)
    {
        if (isHorizontal())
            setY(top);
        else
            setX(top);
    }

    // The logical width is our extent in the line's overall inline direction, i.e., width for horizontal text and height for vertical text.
    void setLogicalWidth(LayoutUnit w) { m_logicalWidth = w; }
    LayoutUnit logicalWidth() const { return m_logicalWidth; }

    // The logical height is our extent in the block flow direction, i.e., height for horizontal text and width for vertical text.
    LayoutUnit logicalHeight() const;

    LayoutRect logicalFrameRect() const { return isHorizontal() ? LayoutRect(m_topLeft.x(), m_topLeft.y(), m_logicalWidth, logicalHeight()) : LayoutRect(m_topLeft.y(), m_topLeft.x(), m_logicalWidth, logicalHeight()); }

    virtual int baselinePosition(FontBaseline baselineType) const;
    virtual LayoutUnit lineHeight() const;

    virtual int caretMinOffset() const;
    virtual int caretMaxOffset() const;

    unsigned char bidiLevel() const { return m_bitfields.bidiEmbeddingLevel(); }
    void setBidiLevel(unsigned char level) { m_bitfields.setBidiEmbeddingLevel(level); }
    TextDirection direction() const { return bidiLevel() % 2 ? RTL : LTR; }
    bool isLeftToRightDirection() const { return direction() == LTR; }
    int caretLeftmostOffset() const { return isLeftToRightDirection() ? caretMinOffset() : caretMaxOffset(); }
    int caretRightmostOffset() const { return isLeftToRightDirection() ? caretMaxOffset() : caretMinOffset(); }

    virtual void clearTruncation() { }

    bool isDirty() const { return m_bitfields.dirty(); }
    virtual void markDirty() { m_bitfields.setDirty(true); }

    virtual void dirtyLineBoxes();

    virtual SelectionState getSelectionState() const;

    virtual bool canAccommodateEllipsis(bool ltr, int blockEdge, int ellipsisWidth) const;
    // visibleLeftEdge, visibleRightEdge are in the parent's coordinate system.
    virtual LayoutUnit placeEllipsisBox(bool ltr, LayoutUnit visibleLeftEdge, LayoutUnit visibleRightEdge, LayoutUnit ellipsisWidth, LayoutUnit &truncatedWidth, bool&);

#if ENABLE(ASSERT)
    void setHasBadParent();
#endif

    int expansion() const { return m_bitfields.expansion(); }

    bool visibleToHitTestRequest(const HitTestRequest& request) const { return getLineLayoutItem().visibleToHitTestRequest(request); }

    // Anonymous inline: https://drafts.csswg.org/css2/visuren.html#anonymous
    bool isAnonymousInline() const { return getLineLayoutItem().isText() && getLineLayoutItem().parent() && getLineLayoutItem().parent().isBox(); }
    EVerticalAlign verticalAlign() const { return isAnonymousInline() ? ComputedStyle::initialVerticalAlign() : getLineLayoutItem().style(m_bitfields.firstLine())->verticalAlign(); }

    // Use with caution! The type is not checked!
    LineLayoutBoxModel boxModelObject() const
    {
        if (!getLineLayoutItem().isText())
            return LineLayoutBoxModel(m_lineLayoutItem);
        return LineLayoutBoxModel(nullptr);
    }

    LayoutPoint locationIncludingFlipping() const;

    // Converts from a rect in the logical space of the InlineBox to one in the physical space
    // of the containing block. The logical space of an InlineBox may be transposed for vertical text and
    // flipped for right-to-left text.
    void logicalRectToPhysicalRect(LayoutRect&) const;

    void flipForWritingMode(FloatRect&) const;
    FloatPoint flipForWritingMode(const FloatPoint&) const;
    void flipForWritingMode(LayoutRect&) const;
    LayoutPoint flipForWritingMode(const LayoutPoint&) const;

    bool knownToHaveNoOverflow() const { return m_bitfields.knownToHaveNoOverflow(); }
    void clearKnownToHaveNoOverflow();

    bool dirOverride() const { return m_bitfields.dirOverride(); }
    void setDirOverride(bool dirOverride) { m_bitfields.setDirOverride(dirOverride); }

    // Invalidate display item clients in the whole sub inline box tree.
    void invalidateDisplayItemClientsRecursively();

#define ADD_BOOLEAN_BITFIELD(name, Name) \
    private:\
    unsigned m_##name : 1;\
    public:\
    bool name() const { return m_##name; }\
    void set##Name(bool name) { m_##name = name; }\

    class InlineBoxBitfields {
        DISALLOW_NEW();
    public:
        InlineBoxBitfields(bool firstLine = false, bool constructed = false, bool dirty = false, bool extracted = false, bool isHorizontal = true)
            : m_firstLine(firstLine)
            , m_constructed(constructed)
            , m_bidiEmbeddingLevel(0)
            , m_dirty(dirty)
            , m_extracted(extracted)
            , m_hasVirtualLogicalHeight(false)
            , m_isHorizontal(isHorizontal)
            , m_endsWithBreak(false)
            , m_hasSelectedChildrenOrCanHaveLeadingExpansion(false)
            , m_knownToHaveNoOverflow(true)
            , m_hasEllipsisBoxOrHyphen(false)
            , m_dirOverride(false)
            , m_isText(false)
            , m_determinedIfNextOnLineExists(false)
            , m_nextOnLineExists(false)
            , m_expansion(0)
        {
        }

        // Some of these bits are actually for subclasses and moved here to compact the structures.
        // for this class
        ADD_BOOLEAN_BITFIELD(firstLine, FirstLine);
        ADD_BOOLEAN_BITFIELD(constructed, Constructed);

    private:
        unsigned m_bidiEmbeddingLevel : 6; // The maximium bidi level is 62: http://unicode.org/reports/tr9/#Explicit_Levels_and_Directions

    public:
        unsigned char bidiEmbeddingLevel() const { return m_bidiEmbeddingLevel; }
        void setBidiEmbeddingLevel(unsigned char bidiEmbeddingLevel) { m_bidiEmbeddingLevel = bidiEmbeddingLevel; }

        ADD_BOOLEAN_BITFIELD(dirty, Dirty);
        ADD_BOOLEAN_BITFIELD(extracted, Extracted);
        ADD_BOOLEAN_BITFIELD(hasVirtualLogicalHeight, HasVirtualLogicalHeight);
        ADD_BOOLEAN_BITFIELD(isHorizontal, IsHorizontal);
        // for RootInlineBox
        ADD_BOOLEAN_BITFIELD(endsWithBreak, EndsWithBreak); // Whether the line ends with a <br>.
        // shared between RootInlineBox and InlineTextBox
        ADD_BOOLEAN_BITFIELD(hasSelectedChildrenOrCanHaveLeadingExpansion, HasSelectedChildrenOrCanHaveLeadingExpansion);

        // This boolean will never be set if there is potential for overflow,
        // but it will be eagerly cleared in the opposite case. As such, it's
        // a conservative tracking of the absence of overflow.
        //
        // For whether we have overflow, callers should use m_overflow on
        // InlineFlowBox.
        ADD_BOOLEAN_BITFIELD(knownToHaveNoOverflow, KnownToHaveNoOverflow);
        ADD_BOOLEAN_BITFIELD(hasEllipsisBoxOrHyphen, HasEllipsisBoxOrHyphen);
        // for InlineTextBox
        ADD_BOOLEAN_BITFIELD(dirOverride, DirOverride);
        ADD_BOOLEAN_BITFIELD(isText, IsText); // Whether or not this object represents text with a non-zero height. Includes non-image list markers, text boxes.

    private:
        mutable unsigned m_determinedIfNextOnLineExists : 1;

    public:
        bool determinedIfNextOnLineExists() const { return m_determinedIfNextOnLineExists; }
        void setDeterminedIfNextOnLineExists(bool determinedIfNextOnLineExists) const { m_determinedIfNextOnLineExists = determinedIfNextOnLineExists; }

    private:
        mutable unsigned m_nextOnLineExists : 1;

    public:
        bool nextOnLineExists() const { return m_nextOnLineExists; }
        void setNextOnLineExists(bool nextOnLineExists) const { m_nextOnLineExists = nextOnLineExists; }

    private:
        unsigned m_expansion : 12; // for justified text

    public:
        signed expansion() const { return m_expansion; }
        void setExpansion(signed expansion) { m_expansion = expansion; }
    };
#undef ADD_BOOLEAN_BITFIELD

private:
    // Converts the given (top-left) position from the logical space of the InlineBox to the physical space of the
    // containing block. The size indicates the size of the box whose point is being flipped.
    LayoutPoint logicalPositionToPhysicalPoint(const LayoutPoint&, const LayoutSize&) const;

    InlineBox* m_next; // The next element on the same line as us.
    InlineBox* m_prev; // The previous element on the same line as us.

    InlineFlowBox* m_parent; // The box that contains us.
    LineLayoutItem m_lineLayoutItem;

protected:
    // For RootInlineBox
    bool endsWithBreak() const { return m_bitfields.endsWithBreak(); }
    void setEndsWithBreak(bool endsWithBreak) { m_bitfields.setEndsWithBreak(endsWithBreak); }
    bool hasEllipsisBox() const { return m_bitfields.hasEllipsisBoxOrHyphen(); }
    bool hasSelectedChildren() const { return m_bitfields.hasSelectedChildrenOrCanHaveLeadingExpansion(); }
    void setHasSelectedChildren(bool hasSelectedChildren) { m_bitfields.setHasSelectedChildrenOrCanHaveLeadingExpansion(hasSelectedChildren); }
    void setHasEllipsisBox(bool hasEllipsisBox) { m_bitfields.setHasEllipsisBoxOrHyphen(hasEllipsisBox); }

    // For InlineTextBox
    bool hasHyphen() const { return m_bitfields.hasEllipsisBoxOrHyphen(); }
    void setHasHyphen(bool hasHyphen) { m_bitfields.setHasEllipsisBoxOrHyphen(hasHyphen); }
    bool canHaveLeadingExpansion() const { return m_bitfields.hasSelectedChildrenOrCanHaveLeadingExpansion(); }
    void setCanHaveLeadingExpansion(bool canHaveLeadingExpansion) { m_bitfields.setHasSelectedChildrenOrCanHaveLeadingExpansion(canHaveLeadingExpansion); }
    signed expansion() { return m_bitfields.expansion(); }
    void setExpansion(signed expansion) { m_bitfields.setExpansion(expansion); }

    // For InlineFlowBox and InlineTextBox
    bool extracted() const { return m_bitfields.extracted(); }

    LayoutPoint m_topLeft;
    LayoutUnit m_logicalWidth;

private:
    InlineBoxBitfields m_bitfields;

    DISPLAY_ITEM_CACHE_STATUS_IMPLEMENTATION

#if ENABLE(ASSERT)
    bool m_hasBadParent;
#endif
};

#if !ENABLE(ASSERT)
inline InlineBox::~InlineBox()
{
}
#endif

#if ENABLE(ASSERT)
inline void InlineBox::setHasBadParent()
{
    m_hasBadParent = true;
}
#endif

#define DEFINE_INLINE_BOX_TYPE_CASTS(typeName) \
    DEFINE_TYPE_CASTS(typeName, InlineBox, box, box->is##typeName(), box.is##typeName())

// Allow equality comparisons of InlineBox's by reference or pointer, interchangeably.
DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES(InlineBox)

} // namespace blink

#ifndef NDEBUG
// Outside the WebCore namespace for ease of invocation from gdb.
void showTree(const blink::InlineBox*);
void showLineTree(const blink::InlineBox*);
#endif

#endif // InlineBox_h
