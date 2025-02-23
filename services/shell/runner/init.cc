// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/shell/runner/init.h"

#include <stdint.h>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/debug/debugger.h"
#include "base/files/file_path.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "services/shell/runner/common/switches.h"

#if defined(OS_WIN)
#include <windows.h>
#elif (OS_POSIX)
#include <unistd.h>
#endif

namespace shell {

void InitializeLogging() {
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);
  // To view log output with IDs and timestamps use "adb logcat -v threadtime".
  logging::SetLogItems(true,   // Process ID
                       true,   // Thread ID
                       true,   // Timestamp
                       true);  // Tick count
}

void WaitForDebuggerIfNecessary() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kWaitForDebugger)) {
    std::vector<std::string> apps_to_debug = base::SplitString(
        command_line->GetSwitchValueASCII(switches::kWaitForDebugger), ",",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    std::string app = "launcher";
    if (command_line->HasSwitch(switches::kChildProcess)) {
      app = command_line->GetSwitchValuePath(switches::kChildProcess)
                .BaseName()
                .RemoveExtension()
                .MaybeAsASCII();
    } else {
      base::FilePath exe_path =
          command_line->GetProgram().BaseName().RemoveExtension();
      for (const auto& app_name : apps_to_debug) {
        if (base::FilePath().AppendASCII(app_name) == exe_path) {
          app = app_name;
          break;
        }
      }
    }
    if (apps_to_debug.empty() || ContainsValue(apps_to_debug, app)) {
#if defined(OS_WIN)
      base::string16 appw = base::UTF8ToUTF16(app);
      base::string16 message = base::UTF8ToUTF16(
          base::StringPrintf("%s - %d", app.c_str(), GetCurrentProcessId()));
      MessageBox(NULL, message.c_str(), appw.c_str(), MB_OK | MB_SETFOREGROUND);
#else
      LOG(ERROR) << app << " waiting for GDB. pid: " << getpid();
      base::debug::WaitForDebugger(60, true);
#endif
    }
  }
}

void CallLibraryEarlyInitialization(base::NativeLibrary app_library) {
  // Do whatever warming that the mojo application wants.
  typedef void (*LibraryEarlyInitFunction)(const uint8_t*);
  LibraryEarlyInitFunction init_function =
      reinterpret_cast<LibraryEarlyInitFunction>(
          base::GetFunctionPointerFromNativeLibrary(app_library,
                                                    "InitializeBase"));
  if (init_function) {
    // Get the ICU data that we prewarmed in the runner and then pass it to
    // the copy of icu in the mojo binary that we're running.
    const uint8_t* icu_data = base::i18n::GetRawIcuMemory();
    init_function(icu_data);
  }

  // TODO(erg): All chromium binaries load base. We might want to make a
  // general system for other people.
}

}  // namespace shell
