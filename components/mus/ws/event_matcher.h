// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_WS_EVENT_MATCHER_H_
#define COMPONENTS_MUS_WS_EVENT_MATCHER_H_

#include <stdint.h>

#include "base/macros.h"
#include "components/mus/public/interfaces/event_matcher.mojom.h"
#include "components/mus/public/interfaces/input_event_constants.mojom.h"
#include "components/mus/public/interfaces/input_key_codes.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect_f.h"

namespace mus {
namespace ws {

// Wraps a mojom::EventMatcher and allows events to be tested against it.
class EventMatcher {
 public:
  explicit EventMatcher(const mojom::EventMatcher& matcher);
  ~EventMatcher();

  bool MatchesEvent(const ui::Event& event) const;

  bool Equals(const EventMatcher& other) const;

 private:
  enum MatchFields {
    NONE = 0,
    TYPE = 1 << 0,
    FLAGS = 1 << 1,
    KEYBOARD_CODE = 1 << 2,
    POINTER_KIND = 1 << 3,
    POINTER_LOCATION = 1 << 4,
  };

  uint32_t fields_to_match_;
  ui::EventType event_type_;
  // Bitfields of kEventFlag* and kMouseEventFlag* values in
  // input_event_constants.mojom.
  int event_flags_;
  int ignore_event_flags_;
  uint16_t keyboard_code_;
  ui::EventPointerType pointer_type_;
  gfx::RectF pointer_region_;

  DISALLOW_COPY_AND_ASSIGN(EventMatcher);
};

}  // namespace ws
}  // namespace mus

#endif  // COMPONENTS_MUS_WS_EVENT_MATCHER_H_
