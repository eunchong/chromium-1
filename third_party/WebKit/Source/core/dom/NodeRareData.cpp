/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/dom/NodeRareData.h"

#include "bindings/core/v8/ScriptWrappableVisitor.h"
#include "core/dom/Element.h"
#include "core/dom/ElementRareData.h"
#include "core/frame/FrameHost.h"
#include "core/layout/LayoutObject.h"
#include "platform/heap/Handle.h"

namespace blink {

struct SameSizeAsNodeRareData {
    void* m_pointer;
    Member<void*> m_willbeMember[2];
    unsigned m_bitfields;
};

static_assert(sizeof(NodeRareData) == sizeof(SameSizeAsNodeRareData), "NodeRareData should stay small");

DEFINE_TRACE_AFTER_DISPATCH(NodeRareData)
{
    visitor->trace(m_mutationObserverData);
    // Do not keep empty NodeListsNodeData objects around.
    if (m_nodeLists && m_nodeLists->isEmpty())
        m_nodeLists.clear();
    else
        visitor->trace(m_nodeLists);
}

DEFINE_TRACE(NodeRareData)
{
    if (m_isElementRareData)
        static_cast<ElementRareData*>(this)->traceAfterDispatch(visitor);
    else
        traceAfterDispatch(visitor);
}

DEFINE_TRACE_WRAPPERS(NodeRareData)
{
    if (m_isElementRareData)
        static_cast<const ElementRareData*>(this)->traceWrappersAfterDispatch(visitor);
    else
        traceWrappersAfterDispatch(visitor);
}

DEFINE_TRACE_WRAPPERS_AFTER_DISPATCH(NodeRareData)
{
    visitor->traceWrappers(m_nodeLists);
}

void NodeRareData::finalizeGarbageCollectedObject()
{
    RELEASE_ASSERT(!layoutObject());
    if (m_isElementRareData)
        static_cast<ElementRareData*>(this)->~ElementRareData();
    else
        this->~NodeRareData();
}

void NodeRareData::incrementConnectedSubframeCount()
{
    SECURITY_CHECK((m_connectedFrameCount + 1) <= FrameHost::maxNumberOfFrames);
    ++m_connectedFrameCount;
}

// Ensure the 10 bits reserved for the m_connectedFrameCount cannot overflow
static_assert(FrameHost::maxNumberOfFrames < (1 << NodeRareData::ConnectedFrameCountBits), "Frame limit should fit in rare data count");

} // namespace blink
