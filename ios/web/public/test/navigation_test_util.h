// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_SHELL_PUBLIC_TEST_NAVIGATION_TEST_UTIL_H_
#define IOS_WEB_SHELL_PUBLIC_TEST_NAVIGATION_TEST_UTIL_H_

#import "ios/web/public/web_state/web_state.h"
#include "url/gurl.h"

namespace web {
namespace navigation_test_util {

// Loads |url| in |web_state| with transition of type ui::PAGE_TRANSITION_TYPED.
void LoadUrl(web::WebState* web_state, const GURL& url);

}  // namespace navigation_test_util
}  // namespace web

#endif  // IOS_WEB_SHELL_PUBLIC_TEST_NAVIGATION_TEST_UTIL_H_