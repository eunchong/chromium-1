// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MASH_WM_SHELF_LAYOUT_MANAGER_H_
#define MASH_WM_SHELF_LAYOUT_MANAGER_H_

#include "base/macros.h"
#include "mash/shelf/public/interfaces/shelf_constants.mojom.h"
#include "mash/wm/layout_manager.h"

namespace mash {
namespace wm {

// Lays out the shelf within shelf containers.
class ShelfLayoutManager : public LayoutManager {
 public:
  explicit ShelfLayoutManager(mus::Window* owner);
  ~ShelfLayoutManager() override;

  void SetAlignment(mash::shelf::mojom::Alignment alignment);
  void SetAutoHideBehavior(mash::shelf::mojom::AutoHideBehavior auto_hide);

 private:
  // Overridden from LayoutManager:
  void LayoutWindow(mus::Window* window) override;

  mash::shelf::mojom::Alignment alignment_;
  mash::shelf::mojom::AutoHideBehavior auto_hide_behavior_;

  DISALLOW_COPY_AND_ASSIGN(ShelfLayoutManager);
};

}  // namespace wm
}  // namespace mash

#endif  // MASH_WM_SHELF_LAYOUT_MANAGER_H_
