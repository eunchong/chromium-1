/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#include "platform/v8_inspector/V8FunctionCall.h"

#include "platform/v8_inspector/V8DebuggerImpl.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include "platform/v8_inspector/public/V8DebuggerClient.h"
#include "wtf/PassOwnPtr.h"

#include <v8.h>

namespace blink {

V8FunctionCall::V8FunctionCall(V8DebuggerImpl* debugger, v8::Local<v8::Context> context, v8::Local<v8::Value> value, const String16& name)
    : m_debugger(debugger)
    , m_context(context)
    , m_name(toV8String(context->GetIsolate(), name))
    , m_value(value)
{
}

void V8FunctionCall::appendArgument(v8::Local<v8::Value> value)
{
    m_arguments.append(value);
}

void V8FunctionCall::appendArgument(const String16& argument)
{
    m_arguments.append(toV8String(m_context->GetIsolate(), argument));
}

void V8FunctionCall::appendArgument(int argument)
{
    m_arguments.append(v8::Number::New(m_context->GetIsolate(), argument));
}

void V8FunctionCall::appendArgument(bool argument)
{
    m_arguments.append(argument ? v8::True(m_context->GetIsolate()) : v8::False(m_context->GetIsolate()));
}

void V8FunctionCall::appendUndefinedArgument()
{
    m_arguments.append(v8::Undefined(m_context->GetIsolate()));
}

v8::Local<v8::Value> V8FunctionCall::call(bool& hadException, bool reportExceptions)
{
    v8::TryCatch tryCatch(m_context->GetIsolate());
    tryCatch.SetVerbose(reportExceptions);

    v8::Local<v8::Value> result = callWithoutExceptionHandling();
    hadException = tryCatch.HasCaught();
    return result;
}

v8::Local<v8::Value> V8FunctionCall::callWithoutExceptionHandling()
{
    // TODO(dgozman): get rid of this check.
    if (!m_debugger->client()->isExecutionAllowed())
        return v8::Local<v8::Value>();

    v8::Local<v8::Object> thisObject = v8::Local<v8::Object>::Cast(m_value);
    v8::Local<v8::Value> value;
    if (!thisObject->Get(m_context, m_name).ToLocal(&value))
        return v8::Local<v8::Value>();

    ASSERT(value->IsFunction());

    v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(value);
    OwnPtr<v8::Local<v8::Value>[]> info = adoptArrayPtr(new v8::Local<v8::Value>[m_arguments.size()]);
    for (size_t i = 0; i < m_arguments.size(); ++i) {
        info[i] = m_arguments[i];
        ASSERT(!info[i].IsEmpty());
    }

    v8::MicrotasksScope microtasksScope(m_context->GetIsolate(), v8::MicrotasksScope::kDoNotRunMicrotasks);
    v8::Local<v8::Value> result;
    if (!function->Call(m_context, thisObject, m_arguments.size(), info.get()).ToLocal(&result))
        return v8::Local<v8::Value>();
    return result;
}

v8::Local<v8::Function> V8FunctionCall::function()
{
    v8::TryCatch tryCatch(m_context->GetIsolate());
    v8::Local<v8::Object> thisObject = v8::Local<v8::Object>::Cast(m_value);
    v8::Local<v8::Value> value;
    if (!thisObject->Get(m_context, m_name).ToLocal(&value))
        return v8::Local<v8::Function>();

    ASSERT(value->IsFunction());
    return v8::Local<v8::Function>::Cast(value);
}

} // namespace blink
