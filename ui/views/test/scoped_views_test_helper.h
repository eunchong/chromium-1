// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_TEST_SCOPED_VIEWS_TEST_HELPER_H_
#define UI_VIEWS_TEST_SCOPED_VIEWS_TEST_HELPER_H_

#include <memory>

#include "base/macros.h"
#include "ui/gfx/native_widget_types.h"

namespace views {

class PlatformTestHelper;
class TestViewsDelegate;
class ViewsTestHelper;

// Creates a ViewsTestHelper that is destroyed automatically. Acts like
// ViewsTestBase but allows a test harness to use a different base class, or
// make use of a TestBrowserThreadBundle, rather than the MessageLoop provided
// by ViewsTestBase.
class ScopedViewsTestHelper {
 public:
  // Initialize with the default TestViewsDelegate, MessageLoopForUI::current()
  // and the default test ContextFactory.
  ScopedViewsTestHelper();

  // Initialize with the given TestViewsDelegate instance, after setting the
  // ContextFactory.
  explicit ScopedViewsTestHelper(
      std::unique_ptr<TestViewsDelegate> views_delegate);

  ~ScopedViewsTestHelper();

  // Returns the context for creating new windows. In Aura builds, this will be
  // the RootWindow. Everywhere else, null.
  gfx::NativeWindow GetContext();

  TestViewsDelegate* views_delegate() { return views_delegate_.get(); };

  bool IsMus() const;

 private:
  std::unique_ptr<TestViewsDelegate> views_delegate_;
  std::unique_ptr<ViewsTestHelper> test_helper_;
  std::unique_ptr<PlatformTestHelper> platform_test_helper_;

  DISALLOW_COPY_AND_ASSIGN(ScopedViewsTestHelper);
};

}  // namespace views

#endif  // UI_VIEWS_TEST_SCOPED_VIEWS_TEST_HELPER_H_
