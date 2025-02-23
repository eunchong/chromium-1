/*
 * Copyright (C) 2009-2010 Google Inc. All rights reserved.
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

#ifndef CrossThreadTask_h
#define CrossThreadTask_h

#include "core/dom/ExecutionContext.h"
#include "core/dom/ExecutionContextTask.h"
#include "platform/ThreadSafeFunctional.h"
#include "wtf/PassOwnPtr.h"
#include <type_traits>

namespace blink {

// createCrossThreadTask(...) is ExecutionContextTask version of
// threadSafeBind().
// Using WTF::bind() directly is not thread-safe due to temporary objects, see
// https://crbug.com/390851 for details.
//
// Example:
//     void func1(int, const String&);
//     createCrossThreadTask(func1, 42, str);
// func1(42, str2) will be called, where |str2| is a deep copy of
// |str| (created by str.isolatedCopy()).
//
// Don't (if you pass the task across threads):
//     bind(func1, 42, str);
//     bind(func1, 42, str.isolatedCopy());
//
// For functions:
//     void functionEC(MP1, ..., MPn, ExecutionContext*);
//     void function(MP1, ..., MPn);
//     class C {
//         void memberEC(MP1, ..., MPn, ExecutionContext*);
//         void member(MP1, ..., MPn);
//     };
// We create tasks represented by std::unique_ptr<ExecutionContextTask>:
//     createCrossThreadTask(functionEC, const P1& p1, ..., const Pn& pn);
//     createCrossThreadTask(memberEC, C* ptr, const P1& p1, ..., const Pn& pn);
//     createCrossThreadTask(function, const P1& p1, ..., const Pn& pn);
//     createCrossThreadTask(member, C* ptr, const P1& p1, ..., const Pn& pn);
// (|ptr| can also be WeakPtr<C> or other pointer-like types)
// and then the following are called on the target thread:
//     functionEC(p1, ..., pn, context);
//     ptr->memberEC(p1, ..., pn, context);
//     function(p1, ..., pn);
//     ptr->member(p1, ..., pn);
//
// ExecutionContext:
//     |context| is supplied by the target thread.
//
// Deep copies by threadSafeBind():
//     |ptr|, |p1|, ..., |pn| are processed by threadSafeBind() and thus
//     CrossThreadCopier.
//     You don't have to call manually e.g. isolatedCopy().
//     To pass things that cannot be copied by CrossThreadCopier
//     (e.g. pointers), use AllowCrossThreadAccess() explicitly.

// RETTYPE, PS, and MPS are added as template parameters to circumvent MSVC 18.00.21005.1 (VS 2013) issues.

template<typename... P, typename... MP,
    typename RETTYPE = std::unique_ptr<ExecutionContextTask>, size_t PS = sizeof...(P), size_t MPS = sizeof...(MP)>
typename std::enable_if<PS + 1 == MPS, RETTYPE>::type createCrossThreadTask(void (*function)(MP...), P&&... parameters)
{
    return internal::CallClosureWithExecutionContextTask<WTF::CrossThreadAffinity>::create(threadSafeBind<ExecutionContext*>(function, std::forward<P>(parameters)...));
}

template<typename... P, typename... MP,
    typename RETTYPE = std::unique_ptr<ExecutionContextTask>, size_t PS = sizeof...(P), size_t MPS = sizeof...(MP)>
typename std::enable_if<PS == MPS, RETTYPE>::type createCrossThreadTask(void (*function)(MP...), P&&... parameters)
{
    return internal::CallClosureTask<WTF::CrossThreadAffinity>::create(threadSafeBind(function, std::forward<P>(parameters)...));
}

template<typename C, typename... P, typename... MP,
    typename RETTYPE = std::unique_ptr<ExecutionContextTask>, size_t PS = sizeof...(P), size_t MPS = sizeof...(MP)>
typename std::enable_if<PS == MPS, RETTYPE>::type createCrossThreadTask(void (C::*function)(MP...), P&&... parameters)
{
    return internal::CallClosureWithExecutionContextTask<WTF::CrossThreadAffinity>::create(threadSafeBind<ExecutionContext*>(function, std::forward<P>(parameters)...));
}

template<typename C, typename... P, typename... MP,
    typename RETTYPE = std::unique_ptr<ExecutionContextTask>, size_t PS = sizeof...(P), size_t MPS = sizeof...(MP)>
typename std::enable_if<PS == MPS + 1, RETTYPE>::type createCrossThreadTask(void (C::*function)(MP...), P&&... parameters)
{
    return internal::CallClosureTask<WTF::CrossThreadAffinity>::create(threadSafeBind(function, std::forward<P>(parameters)...));
}

} // namespace blink

#endif // CrossThreadTask_h
