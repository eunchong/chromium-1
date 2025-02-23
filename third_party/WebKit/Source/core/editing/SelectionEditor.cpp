/*
 * Copyright (C) 2004, 2008, 2009, 2010 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/SelectionEditor.h"

#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/SelectionAdjuster.h"
#include "core/events/Event.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/layout/LayoutBlock.h"
#include "core/layout/line/InlineTextBox.h"
#include "core/page/SpatialNavigation.h"

namespace blink {

static inline LayoutUnit NoXPosForVerticalArrowNavigation()
{
    return LayoutUnit::min();
}

static inline bool shouldAlwaysUseDirectionalSelection(LocalFrame* frame)
{
    return !frame || frame->editor().behavior().shouldConsiderSelectionAsDirectional();
}

SelectionEditor::SelectionEditor(FrameSelection& frameSelection)
    : m_frameSelection(frameSelection)
    , m_xPosForVerticalArrowNavigation(NoXPosForVerticalArrowNavigation())
    , m_observingVisibleSelection(false)
{
}

SelectionEditor::~SelectionEditor()
{
}

void SelectionEditor::dispose()
{
    stopObservingVisibleSelectionChangeIfNecessary();
    if (m_logicalRange) {
        m_logicalRange->dispose();
        m_logicalRange = nullptr;
    }
}

LocalFrame* SelectionEditor::frame() const
{
    return m_frameSelection->frame();
}

template <>
const VisibleSelection& SelectionEditor::visibleSelection<EditingStrategy>() const
{
    return m_selection;
}

template <>
const VisibleSelectionInFlatTree& SelectionEditor::visibleSelection<EditingInFlatTreeStrategy>() const
{
    return m_selectionInFlatTree;
}

void SelectionEditor::setVisibleSelection(const VisibleSelection& newSelection, FrameSelection::SetSelectionOptions options)
{
    m_selection = newSelection;
    if (options & FrameSelection::DoNotAdjustInFlatTree) {
        m_selectionInFlatTree.setWithoutValidation(toPositionInFlatTree(m_selection.base()), toPositionInFlatTree(m_selection.extent()));
        return;
    }

    SelectionAdjuster::adjustSelectionInFlatTree(&m_selectionInFlatTree, m_selection);
}

void SelectionEditor::setVisibleSelection(const VisibleSelectionInFlatTree& newSelection, FrameSelection::SetSelectionOptions options)
{
    DCHECK(!(options & FrameSelection::DoNotAdjustInFlatTree));
    m_selectionInFlatTree = newSelection;
    SelectionAdjuster::adjustSelectionInDOMTree(&m_selection, m_selectionInFlatTree);
}

void SelectionEditor::resetXPosForVerticalArrowNavigation()
{
    m_xPosForVerticalArrowNavigation = NoXPosForVerticalArrowNavigation();
}

void SelectionEditor::setIsDirectional(bool isDirectional)
{
    m_selection.setIsDirectional(isDirectional);
    m_selectionInFlatTree.setIsDirectional(isDirectional);
}

void SelectionEditor::setWithoutValidation(const Position& base, const Position& extent)
{
    m_selection.setWithoutValidation(base, extent);
    m_selectionInFlatTree.setWithoutValidation(toPositionInFlatTree(base), toPositionInFlatTree(extent));
}

TextDirection SelectionEditor::directionOfEnclosingBlock()
{
    return blink::directionOfEnclosingBlock(m_selection.extent());
}

TextDirection SelectionEditor::directionOfSelection()
{
    InlineBox* startBox = nullptr;
    InlineBox* endBox = nullptr;
    // Cache the VisiblePositions because visibleStart() and visibleEnd()
    // can cause layout, which has the potential to invalidate lineboxes.
    VisiblePosition startPosition = m_selection.visibleStart();
    VisiblePosition endPosition = m_selection.visibleEnd();
    if (startPosition.isNotNull())
        startBox = computeInlineBoxPosition(startPosition).inlineBox;
    if (endPosition.isNotNull())
        endBox = computeInlineBoxPosition(endPosition).inlineBox;
    if (startBox && endBox && startBox->direction() == endBox->direction())
        return startBox->direction();

    return directionOfEnclosingBlock();
}

void SelectionEditor::willBeModified(EAlteration alter, SelectionDirection direction)
{
    if (alter != FrameSelection::AlterationExtend)
        return;

    Position start = m_selection.start();
    Position end = m_selection.end();

    bool baseIsStart = true;

    if (m_selection.isDirectional()) {
        // Make base and extent match start and end so we extend the user-visible selection.
        // This only matters for cases where base and extend point to different positions than
        // start and end (e.g. after a double-click to select a word).
        if (m_selection.isBaseFirst())
            baseIsStart = true;
        else
            baseIsStart = false;
    } else {
        switch (direction) {
        case DirectionRight:
            if (directionOfSelection() == LTR)
                baseIsStart = true;
            else
                baseIsStart = false;
            break;
        case DirectionForward:
            baseIsStart = true;
            break;
        case DirectionLeft:
            if (directionOfSelection() == LTR)
                baseIsStart = false;
            else
                baseIsStart = true;
            break;
        case DirectionBackward:
            baseIsStart = false;
            break;
        }
    }
    if (baseIsStart) {
        m_selection.setBase(start);
        m_selection.setExtent(end);
    } else {
        m_selection.setBase(end);
        m_selection.setExtent(start);
    }
    SelectionAdjuster::adjustSelectionInFlatTree(&m_selectionInFlatTree, m_selection);
}

VisiblePosition SelectionEditor::positionForPlatform(bool isGetStart) const
{
    Settings* settings = frame() ? frame()->settings() : nullptr;
    if (settings && settings->editingBehaviorType() == EditingMacBehavior)
        return isGetStart ? m_selection.visibleStart() : m_selection.visibleEnd();
    // Linux and Windows always extend selections from the extent endpoint.
    // FIXME: VisibleSelection should be fixed to ensure as an invariant that
    // base/extent always point to the same nodes as start/end, but which points
    // to which depends on the value of isBaseFirst. Then this can be changed
    // to just return m_sel.extent().
    return m_selection.isBaseFirst() ? m_selection.visibleEnd() : m_selection.visibleStart();
}

VisiblePosition SelectionEditor::startForPlatform() const
{
    return positionForPlatform(true);
}

VisiblePosition SelectionEditor::endForPlatform() const
{
    return positionForPlatform(false);
}

VisiblePosition SelectionEditor::nextWordPositionForPlatform(const VisiblePosition &originalPosition)
{
    VisiblePosition positionAfterCurrentWord = nextWordPosition(originalPosition);

    if (frame() && frame()->editor().behavior().shouldSkipSpaceWhenMovingRight()) {
        // In order to skip spaces when moving right, we advance one
        // word further and then move one word back. Given the
        // semantics of previousWordPosition() this will put us at the
        // beginning of the word following.
        VisiblePosition positionAfterSpacingAndFollowingWord = nextWordPosition(positionAfterCurrentWord);
        if (positionAfterSpacingAndFollowingWord.isNotNull() && positionAfterSpacingAndFollowingWord.deepEquivalent() != positionAfterCurrentWord.deepEquivalent())
            positionAfterCurrentWord = previousWordPosition(positionAfterSpacingAndFollowingWord);

        bool movingBackwardsMovedPositionToStartOfCurrentWord = positionAfterCurrentWord.deepEquivalent() == previousWordPosition(nextWordPosition(originalPosition)).deepEquivalent();
        if (movingBackwardsMovedPositionToStartOfCurrentWord)
            positionAfterCurrentWord = positionAfterSpacingAndFollowingWord;
    }
    return positionAfterCurrentWord;
}

static void adjustPositionForUserSelectAll(VisiblePosition& pos, bool isForward)
{
    if (Node* rootUserSelectAll = EditingStrategy::rootUserSelectAllForNode(pos.deepEquivalent().anchorNode()))
        pos = createVisiblePosition(isForward ? mostForwardCaretPosition(positionAfterNode(rootUserSelectAll), CanCrossEditingBoundary) : mostBackwardCaretPosition(positionBeforeNode(rootUserSelectAll), CanCrossEditingBoundary));
}

VisiblePosition SelectionEditor::modifyExtendingRight(TextGranularity granularity)
{
    VisiblePosition pos = createVisiblePosition(m_selection.extent(), m_selection.affinity());

    // The difference between modifyExtendingRight and modifyExtendingForward is:
    // modifyExtendingForward always extends forward logically.
    // modifyExtendingRight behaves the same as modifyExtendingForward except for extending character or word,
    // it extends forward logically if the enclosing block is LTR direction,
    // but it extends backward logically if the enclosing block is RTL direction.
    switch (granularity) {
    case CharacterGranularity:
        if (directionOfEnclosingBlock() == LTR)
            pos = nextPositionOf(pos, CanSkipOverEditingBoundary);
        else
            pos = previousPositionOf(pos, CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        if (directionOfEnclosingBlock() == LTR)
            pos = nextWordPositionForPlatform(pos);
        else
            pos = previousWordPosition(pos);
        break;
    case LineBoundary:
        if (directionOfEnclosingBlock() == LTR)
            pos = modifyExtendingForward(granularity);
        else
            pos = modifyExtendingBackward(granularity);
        break;
    case SentenceGranularity:
    case LineGranularity:
    case ParagraphGranularity:
    case SentenceBoundary:
    case ParagraphBoundary:
    case DocumentBoundary:
        // FIXME: implement all of the above?
        pos = modifyExtendingForward(granularity);
        break;
    }
    adjustPositionForUserSelectAll(pos, directionOfEnclosingBlock() == LTR);
    return pos;
}

VisiblePosition SelectionEditor::modifyExtendingForward(TextGranularity granularity)
{
    VisiblePosition pos = createVisiblePosition(m_selection.extent(), m_selection.affinity());
    switch (granularity) {
    case CharacterGranularity:
        pos = nextPositionOf(pos, CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        pos = nextWordPositionForPlatform(pos);
        break;
    case SentenceGranularity:
        pos = nextSentencePosition(pos);
        break;
    case LineGranularity:
        pos = nextLinePosition(pos, lineDirectionPointForBlockDirectionNavigation(EXTENT));
        break;
    case ParagraphGranularity:
        pos = nextParagraphPosition(pos, lineDirectionPointForBlockDirectionNavigation(EXTENT));
        break;
    case SentenceBoundary:
        pos = endOfSentence(endForPlatform());
        break;
    case LineBoundary:
        pos = logicalEndOfLine(endForPlatform());
        break;
    case ParagraphBoundary:
        pos = endOfParagraph(endForPlatform());
        break;
    case DocumentBoundary:
        pos = endForPlatform();
        if (isEditablePosition(pos.deepEquivalent()))
            pos = endOfEditableContent(pos);
        else
            pos = endOfDocument(pos);
        break;
    }
    adjustPositionForUserSelectAll(pos, directionOfEnclosingBlock() == LTR);
    return pos;
}

VisiblePosition SelectionEditor::modifyMovingRight(TextGranularity granularity)
{
    VisiblePosition pos;
    switch (granularity) {
    case CharacterGranularity:
        if (m_selection.isRange()) {
            if (directionOfSelection() == LTR)
                pos = createVisiblePosition(m_selection.end(), m_selection.affinity());
            else
                pos = createVisiblePosition(m_selection.start(), m_selection.affinity());
        } else {
            pos = rightPositionOf(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        }
        break;
    case WordGranularity: {
        bool skipsSpaceWhenMovingRight = frame() && frame()->editor().behavior().shouldSkipSpaceWhenMovingRight();
        pos = rightWordPosition(createVisiblePosition(m_selection.extent(), m_selection.affinity()), skipsSpaceWhenMovingRight);
        break;
    }
    case SentenceGranularity:
    case LineGranularity:
    case ParagraphGranularity:
    case SentenceBoundary:
    case ParagraphBoundary:
    case DocumentBoundary:
        // FIXME: Implement all of the above.
        pos = modifyMovingForward(granularity);
        break;
    case LineBoundary:
        pos = rightBoundaryOfLine(startForPlatform(), directionOfEnclosingBlock());
        break;
    }
    return pos;
}

VisiblePosition SelectionEditor::modifyMovingForward(TextGranularity granularity)
{
    VisiblePosition pos;
    // FIXME: Stay in editable content for the less common granularities.
    switch (granularity) {
    case CharacterGranularity:
        if (m_selection.isRange())
            pos = createVisiblePosition(m_selection.end(), m_selection.affinity());
        else
            pos = nextPositionOf(createVisiblePosition(m_selection.extent(), m_selection.affinity()), CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        pos = nextWordPositionForPlatform(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        break;
    case SentenceGranularity:
        pos = nextSentencePosition(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        break;
    case LineGranularity: {
        // down-arrowing from a range selection that ends at the start of a line needs
        // to leave the selection at that line start (no need to call nextLinePosition!)
        pos = endForPlatform();
        if (!m_selection.isRange() || !isStartOfLine(pos))
            pos = nextLinePosition(pos, lineDirectionPointForBlockDirectionNavigation(START));
        break;
    }
    case ParagraphGranularity:
        pos = nextParagraphPosition(endForPlatform(), lineDirectionPointForBlockDirectionNavigation(START));
        break;
    case SentenceBoundary:
        pos = endOfSentence(endForPlatform());
        break;
    case LineBoundary:
        pos = logicalEndOfLine(endForPlatform());
        break;
    case ParagraphBoundary:
        pos = endOfParagraph(endForPlatform());
        break;
    case DocumentBoundary:
        pos = endForPlatform();
        if (isEditablePosition(pos.deepEquivalent()))
            pos = endOfEditableContent(pos);
        else
            pos = endOfDocument(pos);
        break;
    }
    return pos;
}

VisiblePosition SelectionEditor::modifyExtendingLeft(TextGranularity granularity)
{
    VisiblePosition pos = createVisiblePosition(m_selection.extent(), m_selection.affinity());

    // The difference between modifyExtendingLeft and modifyExtendingBackward is:
    // modifyExtendingBackward always extends backward logically.
    // modifyExtendingLeft behaves the same as modifyExtendingBackward except for extending character or word,
    // it extends backward logically if the enclosing block is LTR direction,
    // but it extends forward logically if the enclosing block is RTL direction.
    switch (granularity) {
    case CharacterGranularity:
        if (directionOfEnclosingBlock() == LTR)
            pos = previousPositionOf(pos, CanSkipOverEditingBoundary);
        else
            pos = nextPositionOf(pos, CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        if (directionOfEnclosingBlock() == LTR)
            pos = previousWordPosition(pos);
        else
            pos = nextWordPositionForPlatform(pos);
        break;
    case LineBoundary:
        if (directionOfEnclosingBlock() == LTR)
            pos = modifyExtendingBackward(granularity);
        else
            pos = modifyExtendingForward(granularity);
        break;
    case SentenceGranularity:
    case LineGranularity:
    case ParagraphGranularity:
    case SentenceBoundary:
    case ParagraphBoundary:
    case DocumentBoundary:
        pos = modifyExtendingBackward(granularity);
        break;
    }
    adjustPositionForUserSelectAll(pos, !(directionOfEnclosingBlock() == LTR));
    return pos;
}

VisiblePosition SelectionEditor::modifyExtendingBackward(TextGranularity granularity)
{
    VisiblePosition pos = createVisiblePosition(m_selection.extent(), m_selection.affinity());

    // Extending a selection backward by word or character from just after a table selects
    // the table.  This "makes sense" from the user perspective, esp. when deleting.
    // It was done here instead of in VisiblePosition because we want VPs to iterate
    // over everything.
    switch (granularity) {
    case CharacterGranularity:
        pos = previousPositionOf(pos, CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        pos = previousWordPosition(pos);
        break;
    case SentenceGranularity:
        pos = previousSentencePosition(pos);
        break;
    case LineGranularity:
        pos = previousLinePosition(pos, lineDirectionPointForBlockDirectionNavigation(EXTENT));
        break;
    case ParagraphGranularity:
        pos = previousParagraphPosition(pos, lineDirectionPointForBlockDirectionNavigation(EXTENT));
        break;
    case SentenceBoundary:
        pos = startOfSentence(startForPlatform());
        break;
    case LineBoundary:
        pos = logicalStartOfLine(startForPlatform());
        break;
    case ParagraphBoundary:
        pos = startOfParagraph(startForPlatform());
        break;
    case DocumentBoundary:
        pos = startForPlatform();
        if (isEditablePosition(pos.deepEquivalent()))
            pos = startOfEditableContent(pos);
        else
            pos = startOfDocument(pos);
        break;
    }
    adjustPositionForUserSelectAll(pos, !(directionOfEnclosingBlock() == LTR));
    return pos;
}

VisiblePosition SelectionEditor::modifyMovingLeft(TextGranularity granularity)
{
    VisiblePosition pos;
    switch (granularity) {
    case CharacterGranularity:
        if (m_selection.isRange()) {
            if (directionOfSelection() == LTR)
                pos = createVisiblePosition(m_selection.start(), m_selection.affinity());
            else
                pos = createVisiblePosition(m_selection.end(), m_selection.affinity());
        } else {
            pos = leftPositionOf(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        }
        break;
    case WordGranularity: {
        bool skipsSpaceWhenMovingRight = frame() && frame()->editor().behavior().shouldSkipSpaceWhenMovingRight();
        pos = leftWordPosition(createVisiblePosition(m_selection.extent(), m_selection.affinity()), skipsSpaceWhenMovingRight);
        break;
    }
    case SentenceGranularity:
    case LineGranularity:
    case ParagraphGranularity:
    case SentenceBoundary:
    case ParagraphBoundary:
    case DocumentBoundary:
        // FIXME: Implement all of the above.
        pos = modifyMovingBackward(granularity);
        break;
    case LineBoundary:
        pos = leftBoundaryOfLine(startForPlatform(), directionOfEnclosingBlock());
        break;
    }
    return pos;
}

VisiblePosition SelectionEditor::modifyMovingBackward(TextGranularity granularity)
{
    VisiblePosition pos;
    switch (granularity) {
    case CharacterGranularity:
        if (m_selection.isRange())
            pos = createVisiblePosition(m_selection.start(), m_selection.affinity());
        else
            pos = previousPositionOf(createVisiblePosition(m_selection.extent(), m_selection.affinity()), CanSkipOverEditingBoundary);
        break;
    case WordGranularity:
        pos = previousWordPosition(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        break;
    case SentenceGranularity:
        pos = previousSentencePosition(createVisiblePosition(m_selection.extent(), m_selection.affinity()));
        break;
    case LineGranularity:
        pos = previousLinePosition(startForPlatform(), lineDirectionPointForBlockDirectionNavigation(START));
        break;
    case ParagraphGranularity:
        pos = previousParagraphPosition(startForPlatform(), lineDirectionPointForBlockDirectionNavigation(START));
        break;
    case SentenceBoundary:
        pos = startOfSentence(startForPlatform());
        break;
    case LineBoundary:
        pos = logicalStartOfLine(startForPlatform());
        break;
    case ParagraphBoundary:
        pos = startOfParagraph(startForPlatform());
        break;
    case DocumentBoundary:
        pos = startForPlatform();
        if (isEditablePosition(pos.deepEquivalent()))
            pos = startOfEditableContent(pos);
        else
            pos = startOfDocument(pos);
        break;
    }
    return pos;
}

static bool isBoundary(TextGranularity granularity)
{
    return granularity == LineBoundary || granularity == ParagraphBoundary || granularity == DocumentBoundary;
}

bool SelectionEditor::modify(EAlteration alter, SelectionDirection direction, TextGranularity granularity, EUserTriggered userTriggered)
{
    if (userTriggered == UserTriggered) {
        FrameSelection* trialFrameSelection = FrameSelection::create();
        trialFrameSelection->setSelection(m_selection);
        trialFrameSelection->modify(alter, direction, granularity, NotUserTriggered);

        if (trialFrameSelection->selection().isRange() && m_selection.isCaret() && dispatchSelectStart() != DispatchEventResult::NotCanceled)
            return false;
    }

    willBeModified(alter, direction);

    bool wasRange = m_selection.isRange();
    VisiblePosition originalStartPosition = m_selection.visibleStart();
    VisiblePosition position;
    switch (direction) {
    case DirectionRight:
        if (alter == FrameSelection::AlterationMove)
            position = modifyMovingRight(granularity);
        else
            position = modifyExtendingRight(granularity);
        break;
    case DirectionForward:
        if (alter == FrameSelection::AlterationExtend)
            position = modifyExtendingForward(granularity);
        else
            position = modifyMovingForward(granularity);
        break;
    case DirectionLeft:
        if (alter == FrameSelection::AlterationMove)
            position = modifyMovingLeft(granularity);
        else
            position = modifyExtendingLeft(granularity);
        break;
    case DirectionBackward:
        if (alter == FrameSelection::AlterationExtend)
            position = modifyExtendingBackward(granularity);
        else
            position = modifyMovingBackward(granularity);
        break;
    }

    if (position.isNull())
        return false;

    if (isSpatialNavigationEnabled(frame())) {
        if (!wasRange && alter == FrameSelection::AlterationMove && position.deepEquivalent() == originalStartPosition.deepEquivalent())
            return false;
    }

    // Some of the above operations set an xPosForVerticalArrowNavigation.
    // Setting a selection will clear it, so save it to possibly restore later.
    // Note: the START position type is arbitrary because it is unused, it would be
    // the requested position type if there were no xPosForVerticalArrowNavigation set.
    LayoutUnit x = lineDirectionPointForBlockDirectionNavigation(START);
    m_selection.setIsDirectional(shouldAlwaysUseDirectionalSelection(frame()) || alter == FrameSelection::AlterationExtend);

    switch (alter) {
    case FrameSelection::AlterationMove:
        m_frameSelection->moveTo(position, userTriggered);
        break;
    case FrameSelection::AlterationExtend:

        if (!m_selection.isCaret()
            && (granularity == WordGranularity || granularity == ParagraphGranularity || granularity == LineGranularity)
            && frame() && !frame()->editor().behavior().shouldExtendSelectionByWordOrLineAcrossCaret()) {
            // Don't let the selection go across the base position directly. Needed to match mac
            // behavior when, for instance, word-selecting backwards starting with the caret in
            // the middle of a word and then word-selecting forward, leaving the caret in the
            // same place where it was, instead of directly selecting to the end of the word.
            VisibleSelection newSelection = m_selection;
            newSelection.setExtent(position);
            if (m_selection.isBaseFirst() != newSelection.isBaseFirst())
                position = m_selection.visibleBase();
        }

        // Standard Mac behavior when extending to a boundary is grow the
        // selection rather than leaving the base in place and moving the
        // extent. Matches NSTextView.
        if (!frame() || !frame()->editor().behavior().shouldAlwaysGrowSelectionWhenExtendingToBoundary()
            || m_selection.isCaret()
            || !isBoundary(granularity)) {
            m_frameSelection->setExtent(position, userTriggered);
        } else {
            TextDirection textDirection = directionOfEnclosingBlock();
            if (direction == DirectionForward || (textDirection == LTR && direction == DirectionRight) || (textDirection == RTL && direction == DirectionLeft))
                m_frameSelection->setEnd(position, userTriggered);
            else
                m_frameSelection->setStart(position, userTriggered);
        }
        break;
    }

    if (granularity == LineGranularity || granularity == ParagraphGranularity)
        m_xPosForVerticalArrowNavigation = x;

    return true;
}

// FIXME: Maybe baseline would be better?
static bool absoluteCaretY(const VisiblePosition &c, int &y)
{
    IntRect rect = absoluteCaretBoundsOf(c);
    if (rect.isEmpty())
        return false;
    y = rect.y() + rect.height() / 2;
    return true;
}

bool SelectionEditor::modify(EAlteration alter, unsigned verticalDistance, VerticalDirection direction, EUserTriggered userTriggered, CursorAlignOnScroll align)
{
    if (!verticalDistance)
        return false;

    willBeModified(alter, direction == FrameSelection::DirectionUp ? DirectionBackward : DirectionForward);

    VisiblePosition pos;
    LayoutUnit xPos;
    switch (alter) {
    case FrameSelection::AlterationMove:
        pos = createVisiblePosition(direction == FrameSelection::DirectionUp ? m_selection.start() : m_selection.end(), m_selection.affinity());
        xPos = lineDirectionPointForBlockDirectionNavigation(direction == FrameSelection::DirectionUp ? START : END);
        m_selection.setAffinity(direction == FrameSelection::DirectionUp ? TextAffinity::Upstream : TextAffinity::Downstream);
        break;
    case FrameSelection::AlterationExtend:
        pos = createVisiblePosition(m_selection.extent(), m_selection.affinity());
        xPos = lineDirectionPointForBlockDirectionNavigation(EXTENT);
        m_selection.setAffinity(TextAffinity::Downstream);
        break;
    }

    int startY;
    if (!absoluteCaretY(pos, startY))
        return false;
    if (direction == FrameSelection::DirectionUp)
        startY = -startY;
    int lastY = startY;

    VisiblePosition result;
    VisiblePosition next;
    for (VisiblePosition p = pos; ; p = next) {
        if (direction == FrameSelection::DirectionUp)
            next = previousLinePosition(p, xPos);
        else
            next = nextLinePosition(p, xPos);

        if (next.isNull() || next.deepEquivalent() == p.deepEquivalent())
            break;
        int nextY;
        if (!absoluteCaretY(next, nextY))
            break;
        if (direction == FrameSelection::DirectionUp)
            nextY = -nextY;
        if (nextY - startY > static_cast<int>(verticalDistance))
            break;
        if (nextY >= lastY) {
            lastY = nextY;
            result = next;
        }
    }

    if (result.isNull())
        return false;

    switch (alter) {
    case FrameSelection::AlterationMove:
        m_frameSelection->moveTo(result, userTriggered, align);
        break;
    case FrameSelection::AlterationExtend:
        m_frameSelection->setExtent(result, userTriggered);
        break;
    }

    m_selection.setIsDirectional(shouldAlwaysUseDirectionalSelection(frame()) || alter == FrameSelection::AlterationExtend);

    return true;
}

// Abs x/y position of the caret ignoring transforms.
// TODO(yosin) navigation with transforms should be smarter.
static LayoutUnit lineDirectionPointForBlockDirectionNavigationOf(const VisiblePosition& visiblePosition)
{
    if (visiblePosition.isNull())
        return LayoutUnit();

    LayoutObject* layoutObject;
    LayoutRect localRect = localCaretRectOfPosition(visiblePosition.toPositionWithAffinity(), layoutObject);
    if (localRect.isEmpty() || !layoutObject)
        return LayoutUnit();

    // This ignores transforms on purpose, for now. Vertical navigation is done
    // without consulting transforms, so that 'up' in transformed text is 'up'
    // relative to the text, not absolute 'up'.
    FloatPoint caretPoint = layoutObject->localToAbsolute(FloatPoint(localRect.location()));
    LayoutObject* containingBlock = layoutObject->containingBlock();
    if (!containingBlock)
        containingBlock = layoutObject; // Just use ourselves to determine the writing mode if we have no containing block.
    return LayoutUnit(containingBlock->isHorizontalWritingMode() ? caretPoint.x() : caretPoint.y());
}

LayoutUnit SelectionEditor::lineDirectionPointForBlockDirectionNavigation(EPositionType type)
{
    LayoutUnit x;

    if (m_selection.isNone())
        return x;

    Position pos;
    switch (type) {
    case START:
        pos = m_selection.start();
        break;
    case END:
        pos = m_selection.end();
        break;
    case BASE:
        pos = m_selection.base();
        break;
    case EXTENT:
        pos = m_selection.extent();
        break;
    }

    LocalFrame* frame = pos.document()->frame();
    if (!frame)
        return x;

    if (m_xPosForVerticalArrowNavigation == NoXPosForVerticalArrowNavigation()) {
        VisiblePosition visiblePosition = createVisiblePosition(pos, m_selection.affinity());
        // VisiblePosition creation can fail here if a node containing the selection becomes visibility:hidden
        // after the selection is created and before this function is called.
        x = lineDirectionPointForBlockDirectionNavigationOf(visiblePosition);
        m_xPosForVerticalArrowNavigation = x;
    } else {
        x = m_xPosForVerticalArrowNavigation;
    }

    return x;
}

bool SelectionEditor::setSelectedRange(const EphemeralRange& range, TextAffinity affinity, SelectionDirectionalMode directional, FrameSelection::SetSelectionOptions options)
{
    if (range.isNull())
        return false;

    // Non-collapsed ranges are not allowed to start at the end of a line that is wrapped,
    // they start at the beginning of the next line instead
    if (m_logicalRange) {
        m_logicalRange->dispose();
        m_logicalRange = nullptr;
    }
    stopObservingVisibleSelectionChangeIfNecessary();

    // Since |FrameSeleciton::setSelection()| dispatches events and DOM tree
    // can be modified by event handlers, we should create |Range| object before
    // calling it.
    m_logicalRange = createRange(range);

    VisibleSelection newSelection(range.startPosition(), range.endPosition(), affinity, directional == SelectionDirectionalMode::Directional);
    m_frameSelection->setSelection(newSelection, options);
    startObservingVisibleSelectionChange();
    return true;
}

Range* SelectionEditor::firstRange() const
{
    if (m_logicalRange)
        return m_logicalRange->cloneRange();
    return firstRangeOf(m_selection);
}

DispatchEventResult SelectionEditor::dispatchSelectStart()
{
    Node* selectStartTarget = m_selection.extent().computeContainerNode();
    if (!selectStartTarget)
        return DispatchEventResult::NotCanceled;

    return selectStartTarget->dispatchEvent(Event::createCancelableBubble(EventTypeNames::selectstart));
}

void SelectionEditor::didChangeVisibleSelection()
{
    DCHECK(m_observingVisibleSelection);
    // Invalidate the logical range when the underlying VisibleSelection has changed.
    if (m_logicalRange) {
        m_logicalRange->dispose();
        m_logicalRange = nullptr;
    }
    m_selection.clearChangeObserver();
    m_observingVisibleSelection = false;
}

void SelectionEditor::startObservingVisibleSelectionChange()
{
    DCHECK(!m_observingVisibleSelection);
    m_selection.setChangeObserver(*this);
    m_observingVisibleSelection = true;
}

void SelectionEditor::stopObservingVisibleSelectionChangeIfNecessary()
{
    if (!m_observingVisibleSelection)
        return;
    m_selection.clearChangeObserver();
    m_observingVisibleSelection = false;
}

void SelectionEditor::updateIfNeeded()
{
    m_selection.updateIfNeeded();
    m_selectionInFlatTree.updateIfNeeded();
}

DEFINE_TRACE(SelectionEditor)
{
    visitor->trace(m_frameSelection);
    visitor->trace(m_selection);
    visitor->trace(m_selectionInFlatTree);
    visitor->trace(m_logicalRange);
    VisibleSelectionChangeObserver::trace(visitor);
}

} // namespace blink
