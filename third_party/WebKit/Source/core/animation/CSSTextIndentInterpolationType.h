// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSTextIndentInterpolationType_h
#define CSSTextIndentInterpolationType_h

#include "core/animation/CSSInterpolationType.h"

namespace blink {

class CSSTextIndentInterpolationType : public CSSInterpolationType {
public:
    CSSTextIndentInterpolationType(CSSPropertyID property)
        : CSSInterpolationType(property)
    {
        ASSERT(property == CSSPropertyTextIndent);
    }

    InterpolationValue maybeConvertUnderlyingValue(const InterpolationEnvironment&) const final;
    PairwiseInterpolationValue maybeMergeSingles(InterpolationValue&& start, InterpolationValue&& end) const final;
    void composite(UnderlyingValueOwner&, double underlyingFraction, const InterpolationValue&, double interpolationFraction) const final;
    void apply(const InterpolableValue&, const NonInterpolableValue*, InterpolationEnvironment&) const final;

private:
    InterpolationValue maybeConvertNeutral(const InterpolationValue& underlying, ConversionCheckers&) const final;
    InterpolationValue maybeConvertInitial(const StyleResolverState&) const final;
    InterpolationValue maybeConvertInherit(const StyleResolverState&, ConversionCheckers&) const final;
    InterpolationValue maybeConvertValue(const CSSValue&, const StyleResolverState&, ConversionCheckers&) const final;
};

} // namespace blink

#endif // CSSTextIndentInterpolationType_h
