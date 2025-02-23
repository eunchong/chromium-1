// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSTranslation_h
#define CSSTranslation_h

#include "core/css/cssom/LengthValue.h"
#include "core/css/cssom/TransformComponent.h"

namespace blink {

class ExceptionState;

class CORE_EXPORT CSSTranslation final : public TransformComponent {
    WTF_MAKE_NONCOPYABLE(CSSTranslation);
    DEFINE_WRAPPERTYPEINFO();
public:
    static CSSTranslation* create(LengthValue* x, LengthValue* y, ExceptionState&)
    {
        return new CSSTranslation(x, y, nullptr);
    }
    static CSSTranslation* create(LengthValue* x, LengthValue* y, LengthValue* z, ExceptionState&);

    LengthValue* x() const { return m_x; }
    LengthValue* y() const { return m_y; }
    LengthValue* z() const { return m_z; }

    TransformComponentType type() const override { return is2D() ? TranslationType : Translation3DType; }

    // TODO: Implement asMatrix for CSSTranslation.
    MatrixTransformComponent* asMatrix() const override { return nullptr; }

    CSSFunctionValue* toCSSValue() const override;

    DEFINE_INLINE_VIRTUAL_TRACE()
    {
        visitor->trace(m_x);
        visitor->trace(m_y);
        visitor->trace(m_z);
        TransformComponent::trace(visitor);
    }

private:
    CSSTranslation(LengthValue* x, LengthValue* y, LengthValue* z)
        : TransformComponent()
        , m_x(x)
        , m_y(y)
        , m_z(z)
    { }

    bool is2D() const { return !m_z; }

    Member<LengthValue> m_x;
    Member<LengthValue> m_y;
    Member<LengthValue> m_z;
};

} // namespace blink

#endif
