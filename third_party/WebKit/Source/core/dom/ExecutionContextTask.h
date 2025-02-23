/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
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

#ifndef ExecutionContextTask_h
#define ExecutionContextTask_h

#include "core/CoreExport.h"
#include "wtf/Allocator.h"
#include "wtf/Functional.h"
#include "wtf/Noncopyable.h"
#include "wtf/PtrUtil.h"
#include "wtf/text/WTFString.h"

namespace blink {

class ExecutionContext;

class CORE_EXPORT ExecutionContextTask {
    WTF_MAKE_NONCOPYABLE(ExecutionContextTask);
    USING_FAST_MALLOC(ExecutionContextTask);
public:
    ExecutionContextTask() { }
    virtual ~ExecutionContextTask() { }
    virtual void performTask(ExecutionContext*) = 0;
    virtual String taskNameForInstrumentation() const { return String(); }
};

namespace internal {

template<typename T, WTF::FunctionThreadAffinity threadAffinity>
class CallClosureTaskBase : public ExecutionContextTask {
protected:
    explicit CallClosureTaskBase(std::unique_ptr<Function<T, threadAffinity>> closure)
        : m_closure(std::move(closure))
    {
    }

    std::unique_ptr<Function<T, threadAffinity>> m_closure;
};

template<WTF::FunctionThreadAffinity threadAffinity>
class CallClosureTask final : public CallClosureTaskBase<void(), threadAffinity> {
public:
    // Do not use |create| other than in createCrossThreadTask and
    // createSameThreadTask.
    // See http://crbug.com/390851
    static std::unique_ptr<CallClosureTask<threadAffinity>> create(std::unique_ptr<Function<void(), threadAffinity>> closure)
    {
        return wrapUnique(new CallClosureTask<threadAffinity>(std::move(closure)));
    }

    void performTask(ExecutionContext*) override
    {
        (*this->m_closure)();
    }

private:
    explicit CallClosureTask(std::unique_ptr<Function<void(), threadAffinity>> closure)
        : CallClosureTaskBase<void(), threadAffinity>(std::move(closure))
    {
    }
};

template<WTF::FunctionThreadAffinity threadAffinity>
class CallClosureWithExecutionContextTask final : public CallClosureTaskBase<void(ExecutionContext*), threadAffinity> {
public:
    // Do not use |create| other than in createCrossThreadTask and
    // createSameThreadTask.
    // See http://crbug.com/390851
    static std::unique_ptr<CallClosureWithExecutionContextTask> create(std::unique_ptr<Function<void(ExecutionContext*), threadAffinity>> closure)
    {
        return wrapUnique(new CallClosureWithExecutionContextTask(std::move(closure)));
    }

    void performTask(ExecutionContext* context) override
    {
        (*this->m_closure)(context);
    }

private:
    explicit CallClosureWithExecutionContextTask(std::unique_ptr<Function<void(ExecutionContext*), threadAffinity>> closure)
        : CallClosureTaskBase<void(ExecutionContext*), threadAffinity>(std::move(closure))
    {
    }
};

} // namespace internal

// Create tasks passed within a single thread.
// When posting tasks within a thread, use |createSameThreadTask| instead
// of using |bind| directly to state explicitly that there is no need to care
// about thread safety when posting the task.
// When posting tasks across threads, use |createCrossThreadTask|.
template<typename FunctionType, typename... P>
std::unique_ptr<ExecutionContextTask> createSameThreadTask(
    FunctionType function, P&&... parameters)
{
    return internal::CallClosureTask<WTF::SameThreadAffinity>::create(bind(function, std::forward<P>(parameters)...));
}

} // namespace blink

#endif
