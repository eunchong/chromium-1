// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated by code_generator_v8.py. DO NOT MODIFY!

#include "TestInterfaceOrTestInterfaceEmpty.h"

#include "bindings/core/v8/ToV8.h"
#include "bindings/core/v8/V8TestInterface.h"
#include "bindings/core/v8/V8TestInterfaceEmpty.h"
#include "bindings/tests/idls/core/TestImplements2.h"
#include "bindings/tests/idls/core/TestImplements3Implementation.h"
#include "bindings/tests/idls/core/TestPartialInterface.h"
#include "bindings/tests/idls/core/TestPartialInterfaceImplementation.h"

namespace blink {

TestInterfaceOrTestInterfaceEmpty::TestInterfaceOrTestInterfaceEmpty()
    : m_type(SpecificTypeNone)
{
}

TestInterfaceImplementation* TestInterfaceOrTestInterfaceEmpty::getAsTestInterface() const
{
    ASSERT(isTestInterface());
    return m_testInterface;
}

void TestInterfaceOrTestInterfaceEmpty::setTestInterface(TestInterfaceImplementation* value)
{
    ASSERT(isNull());
    m_testInterface = value;
    m_type = SpecificTypeTestInterface;
}

TestInterfaceOrTestInterfaceEmpty TestInterfaceOrTestInterfaceEmpty::fromTestInterface(TestInterfaceImplementation* value)
{
    TestInterfaceOrTestInterfaceEmpty container;
    container.setTestInterface(value);
    return container;
}

TestInterfaceEmpty* TestInterfaceOrTestInterfaceEmpty::getAsTestInterfaceEmpty() const
{
    ASSERT(isTestInterfaceEmpty());
    return m_testInterfaceEmpty;
}

void TestInterfaceOrTestInterfaceEmpty::setTestInterfaceEmpty(TestInterfaceEmpty* value)
{
    ASSERT(isNull());
    m_testInterfaceEmpty = value;
    m_type = SpecificTypeTestInterfaceEmpty;
}

TestInterfaceOrTestInterfaceEmpty TestInterfaceOrTestInterfaceEmpty::fromTestInterfaceEmpty(TestInterfaceEmpty* value)
{
    TestInterfaceOrTestInterfaceEmpty container;
    container.setTestInterfaceEmpty(value);
    return container;
}

TestInterfaceOrTestInterfaceEmpty::TestInterfaceOrTestInterfaceEmpty(const TestInterfaceOrTestInterfaceEmpty&) = default;
TestInterfaceOrTestInterfaceEmpty::~TestInterfaceOrTestInterfaceEmpty() = default;
TestInterfaceOrTestInterfaceEmpty& TestInterfaceOrTestInterfaceEmpty::operator=(const TestInterfaceOrTestInterfaceEmpty&) = default;

DEFINE_TRACE(TestInterfaceOrTestInterfaceEmpty)
{
    visitor->trace(m_testInterface);
    visitor->trace(m_testInterfaceEmpty);
}

void V8TestInterfaceOrTestInterfaceEmpty::toImpl(v8::Isolate* isolate, v8::Local<v8::Value> v8Value, TestInterfaceOrTestInterfaceEmpty& impl, UnionTypeConversionMode conversionMode, ExceptionState& exceptionState)
{
    if (v8Value.IsEmpty())
        return;

    if (conversionMode == UnionTypeConversionMode::Nullable && isUndefinedOrNull(v8Value))
        return;

    if (V8TestInterface::hasInstance(v8Value, isolate)) {
        TestInterfaceImplementation* cppValue = V8TestInterface::toImpl(v8::Local<v8::Object>::Cast(v8Value));
        impl.setTestInterface(cppValue);
        return;
    }

    if (V8TestInterfaceEmpty::hasInstance(v8Value, isolate)) {
        TestInterfaceEmpty* cppValue = V8TestInterfaceEmpty::toImpl(v8::Local<v8::Object>::Cast(v8Value));
        impl.setTestInterfaceEmpty(cppValue);
        return;
    }

    exceptionState.throwTypeError("The provided value is not of type '(TestInterface or TestInterfaceEmpty)'");
}

v8::Local<v8::Value> toV8(const TestInterfaceOrTestInterfaceEmpty& impl, v8::Local<v8::Object> creationContext, v8::Isolate* isolate)
{
    switch (impl.m_type) {
    case TestInterfaceOrTestInterfaceEmpty::SpecificTypeNone:
        return v8::Null(isolate);
    case TestInterfaceOrTestInterfaceEmpty::SpecificTypeTestInterface:
        return toV8(impl.getAsTestInterface(), creationContext, isolate);
    case TestInterfaceOrTestInterfaceEmpty::SpecificTypeTestInterfaceEmpty:
        return toV8(impl.getAsTestInterfaceEmpty(), creationContext, isolate);
    default:
        ASSERT_NOT_REACHED();
    }
    return v8::Local<v8::Value>();
}

TestInterfaceOrTestInterfaceEmpty NativeValueTraits<TestInterfaceOrTestInterfaceEmpty>::nativeValue(v8::Isolate* isolate, v8::Local<v8::Value> value, ExceptionState& exceptionState)
{
    TestInterfaceOrTestInterfaceEmpty impl;
    V8TestInterfaceOrTestInterfaceEmpty::toImpl(isolate, value, impl, UnionTypeConversionMode::NotNullable, exceptionState);
    return impl;
}

} // namespace blink
