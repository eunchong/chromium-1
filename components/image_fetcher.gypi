# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      # GN version: //components/image_fetcher
      'target_name': 'image_fetcher',
      'type': 'none',
      'include_dirs': [
        '..',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        '../url/url.gyp:url_lib',
      ],
      'sources': [
        'image_fetcher/image_fetcher.h',
        'image_fetcher/image_fetcher_delegate.h',
      ]
    },
  ],
}
