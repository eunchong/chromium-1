# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'os_crypt',
      'type': 'static_library',
      'include_dirs': [
        '..',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        '../crypto/crypto.gyp:crypto',
      ],
      'sources': [
        'os_crypt/ie7_password_win.cc',
        'os_crypt/ie7_password_win.h',
        'os_crypt/keychain_password_mac.h',
        'os_crypt/keychain_password_mac.mm',
        'os_crypt/os_crypt.h',
        'os_crypt/os_crypt_mac.mm',
        'os_crypt/os_crypt_posix.cc',
        'os_crypt/os_crypt_switches.cc',
        'os_crypt/os_crypt_switches.h',
        'os_crypt/os_crypt_win.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': [
            'os_crypt/os_crypt_posix.cc',
          ],
        }],
        ['OS=="win"', {
          'all_dependent_settings': {
            'msvs_settings': {
              'VCLinkerTool': {
                'AdditionalDependencies': [
                  'crypt32.lib',
                ],
              },
            },
          },
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies': [
                'crypt32.lib',
              ],
            },
          },
        }],
        ['OS=="linux" and chromeos!=1', {
          'sources': [ 
            'os_crypt/libsecret_util_posix.cc',
            'os_crypt/libsecret_util_posix.h',
          ],
          'defines': [
            'USE_LIBSECRET',
          ],
          'include_dirs' : [
            '../third_party/libsecret/'
          ],
          'dependencies': [
            '../build/linux/system.gyp:glib',
          ],
        }],
      ],
      'target_conditions': [
        ['OS=="ios"', {
          'sources/': [
            ['include', '^os_crypt/keychain_password_mac\\.mm$'],
            ['include', '^os_crypt/os_crypt_mac\\.mm$'],
          ],
        }],
      ],
    },
  ],
}
