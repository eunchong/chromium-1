// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_VR_TEST_FAKE_VR_DEVICE_H_
#define CONTENT_BROWSER_VR_TEST_FAKE_VR_DEVICE_H_

#include "base/macros.h"
#include "content/browser/vr/vr_device.h"
#include "content/browser/vr/vr_device_provider.h"

namespace content {

class FakeVRDevice : public VRDevice {
 public:
  explicit FakeVRDevice(VRDeviceProvider* provider);
  ~FakeVRDevice() override;

  void SetVRDevice(const mojom::VRDeviceInfoPtr& device);
  void SetSensorState(const mojom::VRSensorStatePtr& state);

  mojom::VRDeviceInfoPtr GetVRDevice() override;
  mojom::VRSensorStatePtr GetSensorState() override;
  void ResetSensor() override{};

 private:
  mojom::VRDeviceInfoPtr device_;
  mojom::VRSensorStatePtr state_;

  DISALLOW_COPY_AND_ASSIGN(FakeVRDevice);
};

}  // namespace content

#endif  // CONTENT_BROWSER_VR_TEST_FAKE_VR_DEVICE_H_
