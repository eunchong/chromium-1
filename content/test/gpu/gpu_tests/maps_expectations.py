# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from gpu_tests.gpu_test_expectations import GpuTestExpectations

# See the GpuTestExpectations class for documentation.

class MapsExpectations(GpuTestExpectations):
  def SetExpectations(self):
    # Sample Usage:
    # self.Fail('Maps.maps_001',
    #     ['mac', 'amd', ('nvidia', 0x1234)], bug=123)

    # Nexus 5X
    self.Fail('Maps.maps_002',
              ['android', ('qualcomm', 'Adreno (TM) 418')], bug=610951)

    # Nexus 9
    self.Fail('Maps.maps_002',
          ['android', 'nvidia'], bug=610034)

