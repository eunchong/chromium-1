// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FakeDisplayItemClient_h
#define FakeDisplayItemClient_h

#include "platform/geometry/LayoutRect.h"
#include "wtf/Forward.h"

namespace blink {

// A simple DisplayItemClient implementation suitable for use in unit tests.
class FakeDisplayItemClient : public DisplayItemClient {
public:
    FakeDisplayItemClient(const String& name = "FakeDisplayItemClient", const LayoutRect& visualRect = LayoutRect())
        : m_name(name), m_visualRect(visualRect)
    { }

    String debugName() const final { return m_name; }
    LayoutRect visualRect() const override { return m_visualRect; }

private:
    DISPLAY_ITEM_CACHE_STATUS_IMPLEMENTATION

    String m_name;
    LayoutRect m_visualRect;
};

} // namespace blink

#endif // FakeDisplayItemClient_h
