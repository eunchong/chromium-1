// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSKeywordValue_h
#define CSSKeywordValue_h

#include "core/CoreExport.h"
#include "core/css/cssom/StyleValue.h"

namespace blink {

class ExceptionState;

class CORE_EXPORT CSSKeywordValue final : public StyleValue {
    WTF_MAKE_NONCOPYABLE(CSSKeywordValue);
    DEFINE_WRAPPERTYPEINFO();
public:
    static CSSKeywordValue* create(const String& keyword, ExceptionState&);

    StyleValueType type() const override { return KeywordType; }

    const String& keywordValue() const;
    CSSValueID keywordValueID() const;

    CSSValue* toCSSValue() const override;

private:
    CSSKeywordValue(const String& keyword) : m_keywordValue(keyword) {}

    String m_keywordValue;
};

DEFINE_TYPE_CASTS(CSSKeywordValue, StyleValue, value,
    value->type() == StyleValue::StyleValueType::KeywordType,
    value.type() == StyleValue::StyleValueType::KeywordType);

} // namespace blink

#endif
