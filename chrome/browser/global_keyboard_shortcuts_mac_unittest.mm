// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <AppKit/NSEvent.h>
#include <Carbon/Carbon.h>
#include <stddef.h>

#include "chrome/browser/global_keyboard_shortcuts_mac.h"

#include "base/macros.h"
#include "chrome/app/chrome_command_ids.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(GlobalKeyboardShortcuts, ShortcutsToWindowCommand) {
  // Test that an invalid shortcut translates into an invalid command id.
  EXPECT_EQ(
      -1, CommandForWindowKeyboardShortcut(false, false, false, false, 0, 0));

  // Check that all known keyboard shortcuts return valid results.
  size_t num_shortcuts = 0;
  const KeyboardShortcutData *it =
      GetWindowKeyboardShortcutTable(&num_shortcuts);
  ASSERT_GT(num_shortcuts, 0U);
  for (size_t i = 0; i < num_shortcuts; ++i, ++it) {
    int cmd_num = CommandForWindowKeyboardShortcut(
        it->command_key, it->shift_key, it->cntrl_key, it->opt_key,
        it->vkey_code, it->key_char);
    EXPECT_EQ(cmd_num, it->chrome_command);
  }

  // Test that cmd-left is not a window-level command (else it
  // would be invoked even if e.g. the omnibox had focus, where it really
  // should have text editing functionality).
  EXPECT_EQ(-1, CommandForWindowKeyboardShortcut(
      true, false, false, false, kVK_LeftArrow, 0));

  // Test that Cmd-'{' and Cmd-'}' are interpreted as IDC_SELECT_NEXT_TAB
  // and IDC_SELECT_PREVIOUS_TAB regardless of the virtual key code values.
  EXPECT_EQ(IDC_SELECT_NEXT_TAB, CommandForWindowKeyboardShortcut(
      true, false, false, false, kVK_ANSI_Period, '}'));
  EXPECT_EQ(IDC_SELECT_PREVIOUS_TAB, CommandForWindowKeyboardShortcut(
      true, true, false, false, kVK_ANSI_Slash, '{'));

  // One more test for Cmd-'{' / Alt-8 (on German keyboard layout).
  EXPECT_EQ(IDC_SELECT_PREVIOUS_TAB, CommandForWindowKeyboardShortcut(
      true, false, false, true, kVK_ANSI_8, '{'));

  // Test that switching tabs triggers off keycodes and not characters (visible
  // with the Italian keyboard layout).
  EXPECT_EQ(IDC_SELECT_TAB_0, CommandForWindowKeyboardShortcut(
      true, false, false, false, kVK_ANSI_1, '&'));
}

TEST(GlobalKeyboardShortcuts, KeypadNumberKeysMatch) {
  // Test that the shortcuts that are generated by keypad number keys match the
  // equivalent keys.
  static const struct {
    int keycode;
    int keypad_keycode;
  } equivalents[] = {
    {kVK_ANSI_0, kVK_ANSI_Keypad0},
    {kVK_ANSI_1, kVK_ANSI_Keypad1},
    {kVK_ANSI_2, kVK_ANSI_Keypad2},
    {kVK_ANSI_3, kVK_ANSI_Keypad3},
    {kVK_ANSI_4, kVK_ANSI_Keypad4},
    {kVK_ANSI_5, kVK_ANSI_Keypad5},
    {kVK_ANSI_6, kVK_ANSI_Keypad6},
    {kVK_ANSI_7, kVK_ANSI_Keypad7},
    {kVK_ANSI_8, kVK_ANSI_Keypad8},
    {kVK_ANSI_9, kVK_ANSI_Keypad9},
  };

  for (unsigned int i = 0; i < arraysize(equivalents); ++i) {
    for (int command = 0; command <= 1; ++command) {
      for (int shift = 0; shift <= 1; ++shift) {
        for (int control = 0; control <= 1; ++control) {
          for (int option = 0; option <= 1; ++option) {
            EXPECT_EQ(
                CommandForWindowKeyboardShortcut(
                  command, shift, control, option, equivalents[i].keycode, 0),
                CommandForWindowKeyboardShortcut(
                  command, shift, control, option,
                  equivalents[i].keypad_keycode, 0));
            EXPECT_EQ(
                CommandForDelayedWindowKeyboardShortcut(
                  command, shift, control, option, equivalents[i].keycode, 0),
                CommandForDelayedWindowKeyboardShortcut(
                  command, shift, control, option,
                  equivalents[i].keypad_keycode, 0));
            EXPECT_EQ(
                CommandForBrowserKeyboardShortcut(
                  command, shift, control, option, equivalents[i].keycode, 0),
                CommandForBrowserKeyboardShortcut(
                  command, shift, control, option,
                  equivalents[i].keypad_keycode, 0));
          }
        }
      }
    }
  }
}

TEST(GlobalKeyboardShortcuts, ShortcutsToDelayedWindowCommand) {
  // Test that an invalid shortcut translates into an invalid command id.
  EXPECT_EQ(-1,
      CommandForDelayedWindowKeyboardShortcut(false, false, false, false,
                                              0, 0));

  // Check that all known keyboard shortcuts return valid results.
  size_t num_shortcuts = 0;
  const KeyboardShortcutData *it =
      GetDelayedWindowKeyboardShortcutTable(&num_shortcuts);
  ASSERT_GT(num_shortcuts, 0U);
  for (size_t i = 0; i < num_shortcuts; ++i, ++it) {
    int cmd_num = CommandForDelayedWindowKeyboardShortcut(
        it->command_key, it->shift_key, it->cntrl_key, it->opt_key,
        it->vkey_code, it->key_char);
    EXPECT_EQ(cmd_num, it->chrome_command);
  }
}

TEST(GlobalKeyboardShortcuts, ShortcutsToBrowserCommand) {
  // Test that an invalid shortcut translates into an invalid command id.
  EXPECT_EQ(
      -1, CommandForBrowserKeyboardShortcut(false, false, false, false,
                                            0, 0));

  // Check that all known keyboard shortcuts return valid results.
  size_t num_shortcuts = 0;
  const KeyboardShortcutData *it =
      GetBrowserKeyboardShortcutTable(&num_shortcuts);
  ASSERT_GT(num_shortcuts, 0U);
  for (size_t i = 0; i < num_shortcuts; ++i, ++it) {
    int cmd_num = CommandForBrowserKeyboardShortcut(
        it->command_key, it->shift_key, it->cntrl_key, it->opt_key,
        it->vkey_code, it->key_char);
    EXPECT_EQ(cmd_num, it->chrome_command);
  }
}

NSEvent* KeyEvent(bool command_key, bool shift_key,
                  bool cntrl_key, bool opt_key,
                  NSString* chars, NSString* charsNoMods) {
  NSUInteger modifierFlags = 0;
  if (command_key)
    modifierFlags |= NSCommandKeyMask;
  if (shift_key)
    modifierFlags |= NSShiftKeyMask;
  if (cntrl_key)
    modifierFlags |= NSControlKeyMask;
  if (opt_key)
    modifierFlags |= NSAlternateKeyMask;
  return [NSEvent keyEventWithType:NSKeyDown
                          location:NSZeroPoint
                     modifierFlags:modifierFlags
                         timestamp:0.0
                      windowNumber:0
                           context:nil
                        characters:chars
       charactersIgnoringModifiers:charsNoMods
                         isARepeat:NO
                           keyCode:0];
}

TEST(GlobalKeyboardShortcuts, KeyCharacterForEvent) {
  // 'a'
  EXPECT_EQ('a', KeyCharacterForEvent(
      KeyEvent(false, false, false, false, @"a", @"a")));
  // cmd-'a' / cmd-shift-'a'
  EXPECT_EQ('a', KeyCharacterForEvent(
      KeyEvent(true,  true,  false, false, @"a", @"A")));
  // '8'
  EXPECT_EQ('8', KeyCharacterForEvent(
      KeyEvent(false, false, false, false, @"8", @"8")));
  // '{' / alt-'8' on german
  EXPECT_EQ('{', KeyCharacterForEvent(
      KeyEvent(false, false, false, true,  @"{", @"8")));
  // cmd-'{' / cmd-shift-'[' on ansi
  EXPECT_EQ('{', KeyCharacterForEvent(
      KeyEvent(true,  true,  false, false, @"[", @"{")));
  // cmd-'z' / cmd-shift-';' on dvorak-qwerty
  EXPECT_EQ('z', KeyCharacterForEvent(
      KeyEvent(true,  true,  false, false, @"z", @":")));
  // cmd-shift-'[' in an RTL context pre 10.9.
  EXPECT_EQ('{', KeyCharacterForEvent(
      KeyEvent(true,  true,  false, false, @"{", @"}")));
  // cmd-shift-'[' in an RTL context on 10.9.
  EXPECT_EQ('{', KeyCharacterForEvent(
      KeyEvent(true,  true,  false, false, @"[", @"}")));
  // Test if getting dead-key events return 0 and do not hang.
  EXPECT_EQ(0,   KeyCharacterForEvent(
      KeyEvent(false, false, false, false, @"",  @"")));
}
