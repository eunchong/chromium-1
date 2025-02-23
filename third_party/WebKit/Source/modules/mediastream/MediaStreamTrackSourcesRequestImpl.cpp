/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GOOGLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/mediastream/MediaStreamTrackSourcesRequestImpl.h"

#include "core/dom/CrossThreadTask.h"
#include "core/dom/ExecutionContext.h"
#include "modules/mediastream/MediaStreamTrackSourcesCallback.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "public/platform/WebSourceInfo.h"
#include "public/platform/WebTraceLocation.h"
#include "wtf/Functional.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

MediaStreamTrackSourcesRequestImpl* MediaStreamTrackSourcesRequestImpl::create(ExecutionContext& context, MediaStreamTrackSourcesCallback* callback)
{
    return new MediaStreamTrackSourcesRequestImpl(context, callback);
}

MediaStreamTrackSourcesRequestImpl::MediaStreamTrackSourcesRequestImpl(ExecutionContext& context, MediaStreamTrackSourcesCallback* callback)
    : m_callback(callback)
    , m_executionContext(&context)
{
}

MediaStreamTrackSourcesRequestImpl::~MediaStreamTrackSourcesRequestImpl()
{
}

PassRefPtr<SecurityOrigin> MediaStreamTrackSourcesRequestImpl::origin()
{
    return m_executionContext->getSecurityOrigin()->isolatedCopy();
}

void MediaStreamTrackSourcesRequestImpl::requestSucceeded(const WebVector<WebSourceInfo>& webSourceInfos)
{
    DCHECK(m_callback);

    for (size_t i = 0; i < webSourceInfos.size(); ++i)
        m_sourceInfos.append(SourceInfo::create(webSourceInfos[i]));
    m_executionContext->postTask(BLINK_FROM_HERE, createCrossThreadTask(&MediaStreamTrackSourcesRequestImpl::performCallback, AllowCrossThreadAccess(this)));
}

void MediaStreamTrackSourcesRequestImpl::performCallback()
{
    m_callback->handleEvent(m_sourceInfos);
    m_callback.clear();
}

DEFINE_TRACE(MediaStreamTrackSourcesRequestImpl)
{
    visitor->trace(m_callback);
    visitor->trace(m_executionContext);
    visitor->trace(m_sourceInfos);
    MediaStreamTrackSourcesRequest::trace(visitor);
}

} // namespace blink
