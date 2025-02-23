// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/lifetime/keep_alive_types.h"
#include "base/logging.h"

std::ostream& operator<<(std::ostream& out, const KeepAliveOrigin& origin) {
  switch (origin) {
    case KeepAliveOrigin::APP_CONTROLLER:
      return out << "APP_CONTROLLER";
    case KeepAliveOrigin::BROWSER:
      return out << "BROWSER";
    case KeepAliveOrigin::BROWSER_PROCESS_CHROMEOS:
      return out << "BROWSER_PROCESS_CHROMEOS";
    case KeepAliveOrigin::SESSION_RESTORE:
      return out << "SESSION_RESTORE";
    case KeepAliveOrigin::BACKGROUND_MODE_MANAGER:
      return out << "BACKGROUND_MODE_MANAGER";
    case KeepAliveOrigin::BACKGROUND_MODE_MANAGER_STARTUP:
      return out << "BACKGROUND_MODE_MANAGER_STARTUP";
    case KeepAliveOrigin::LOGIN_DISPLAY_HOST_IMPL:
      return out << "LOGIN_DISPLAY_HOST_IMPL";
    case KeepAliveOrigin::APP_LIST_SERVICE_VIEWS:
      return out << "APP_LIST_SERVICE_VIEWS";
    case KeepAliveOrigin::APP_LIST_SHOWER:
      return out << "APP_LIST_SHOWER";
    case KeepAliveOrigin::CHROME_APP_DELEGATE:
      return out << "CHROME_APP_DELEGATE";
    case KeepAliveOrigin::CHROME_VIEWS_DELEGATE:
      return out << "CHROME_VIEWS_DELEGATE";
    case KeepAliveOrigin::LEAKED_UNINSTALL_VIEW:
      return out << "LEAKED_UNINSTALL_VIEW";
    case KeepAliveOrigin::PANEL:
      return out << "PANEL";
    case KeepAliveOrigin::PANEL_VIEW:
      return out << "PANEL_VIEW";
    case KeepAliveOrigin::PROFILE_LOADER:
      return out << "PROFILE_LOADER";
    case KeepAliveOrigin::USER_MANAGER_VIEW:
      return out << "USER_MANAGER_VIEW";
  }

  NOTREACHED();
  return out << static_cast<int>(origin);
}

std::ostream& operator<<(std::ostream& out,
                         const KeepAliveRestartOption& restart) {
  switch (restart) {
    case KeepAliveRestartOption::DISABLED:
      return out << "DISABLED";
    case KeepAliveRestartOption::ENABLED:
      return out << "ENABLED";
  }

  NOTREACHED();
  return out << static_cast<int>(restart);
}
