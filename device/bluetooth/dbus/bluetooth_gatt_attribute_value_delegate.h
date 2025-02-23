// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_ATTRIBUTE_VALUE_DELEGATE_H_
#define DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_ATTRIBUTE_VALUE_DELEGATE_H_

#include <cstdint>
#include <vector>

#include "base/callback_forward.h"
#include "device/bluetooth/bluetooth_local_gatt_service.h"

namespace bluez {

// A simpler interface for reacting to GATT attribute value requests by the
// DBus attribute service providers.
class BluetoothGattAttributeValueDelegate {
 public:
  virtual ~BluetoothGattAttributeValueDelegate() {}

  // This method will be called when a remote device requests to read the
  // value of the exported GATT attribute. Invoke |callback| with a value
  // to return that value to the requester. Invoke |error_callback| to report
  // a failure to read the value. This can happen, for example, if the
  // attribute has no read permission set. Either callback should be
  // invoked after a reasonable amount of time, since the request will time
  // out if left pending for too long causing a disconnection.
  virtual void GetValue(
      const device::BluetoothLocalGattService::Delegate::ValueCallback&
          callback,
      const device::BluetoothLocalGattService::Delegate::ErrorCallback&
          error_callback) = 0;

  // This method will be called, when a remote device requests to write the
  // value of the exported GATT attribute. Invoke |callback| to report
  // that the value was successfully written. Invoke |error_callback| to
  // report a failure to write the value. This can happen, for example, if the
  // attribute has no write permission set. Either callback should be
  // invoked after a reasonable amount of time, since the request will time
  // out if left pending for too long causing a disconnection.
  virtual void SetValue(
      const std::vector<uint8_t>& value,
      const base::Closure& callback,
      const device::BluetoothLocalGattService::Delegate::ErrorCallback&
          error_callback) = 0;

  // This method will be called, when a remote device requests to start sending
  // notifications for this characteristic. This will never be called for
  // descriptors.
  virtual void StartNotifications() = 0;

  // This method will be called, when a remote device requests to stop sending
  // notifications for this characteristic. This will never be called for
  // descriptors.
  virtual void StopNotifications() = 0;
};

}  // namespace bluez

#endif  // DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_ATTRIBUTE_VALUE_DELEGATE_H_
