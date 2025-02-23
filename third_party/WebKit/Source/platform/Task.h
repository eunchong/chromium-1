/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#ifndef Task_h
#define Task_h

#include "public/platform/WebTaskRunner.h"
#include "wtf/Allocator.h"
#include "wtf/Functional.h"
#include "wtf/Noncopyable.h"
#include "wtf/OwnPtr.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

// Please use WTF::SameThreadClosure or WTF::CrossThreadClosure directly,
// instead of wrapping them by blink::SameThreadTask/CrossThreadTask here.

// TODO(hiroshige): Make these classes internal to
// Source/platform/WebTaskRunner.cpp.
class SameThreadTask : public WebTaskRunner::Task {
    USING_FAST_MALLOC(SameThreadTask);
    WTF_MAKE_NONCOPYABLE(SameThreadTask);
public:
    explicit SameThreadTask(std::unique_ptr<SameThreadClosure> closure)
        : m_closure(std::move(closure))
    {
    }

    void run() override
    {
        (*m_closure)();
    }

private:
    std::unique_ptr<SameThreadClosure> m_closure;
};

class CrossThreadTask : public WebTaskRunner::Task {
    USING_FAST_MALLOC(CrossThreadTask);
    WTF_MAKE_NONCOPYABLE(CrossThreadTask);
public:
    explicit CrossThreadTask(std::unique_ptr<CrossThreadClosure> closure)
        : m_closure(std::move(closure))
    {
    }

    void run() override
    {
        (*m_closure)();
    }

private:
    std::unique_ptr<CrossThreadClosure> m_closure;
};

} // namespace blink

#endif // Task_h
