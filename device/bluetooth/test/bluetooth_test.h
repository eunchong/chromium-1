// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_H_
#define DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session.h"
#include "device/bluetooth/bluetooth_gatt_connection.h"
#include "device/bluetooth/bluetooth_gatt_notify_session.h"
#include "device/bluetooth/bluetooth_local_gatt_service.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic.h"
#include "device/bluetooth/bluetooth_remote_gatt_descriptor.h"
#include "device/bluetooth/bluetooth_remote_gatt_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace device {

class BluetoothAdapter;
class BluetoothDevice;
class BluetoothLocalGattCharacteristic;
class BluetoothLocalGattDescriptor;

// A test fixture for Bluetooth that abstracts platform specifics for creating
// and controlling fake low level objects.
//
// Subclasses on each platform implement this, and are then typedef-ed to
// BluetoothTest.
class BluetoothTestBase : public testing::Test {
 public:
  enum class Call { EXPECTED, NOT_EXPECTED };

  static const std::string kTestAdapterName;
  static const std::string kTestAdapterAddress;

  static const std::string kTestDeviceName;
  static const std::string kTestDeviceNameEmpty;

  static const std::string kTestDeviceAddress1;
  static const std::string kTestDeviceAddress2;

  static const std::string kTestUUIDGenericAccess;
  static const std::string kTestUUIDGenericAttribute;
  static const std::string kTestUUIDImmediateAlert;
  static const std::string kTestUUIDLinkLoss;

  BluetoothTestBase();
  ~BluetoothTestBase() override;

  // Checks that no unexpected calls have been made to callbacks.
  // Overrides of this method should always call the parent's class method.
  void TearDown() override;

  // Calls adapter_->StartDiscoverySessionWithFilter with Low Energy transport,
  // and this fixture's callbacks expecting success.
  // Then RunLoop().RunUntilIdle().
  virtual void StartLowEnergyDiscoverySession();

  // Calls adapter_->StartDiscoverySessionWithFilter with Low Energy transport,
  // and this fixture's callbacks expecting error.
  // Then RunLoop().RunUntilIdle().
  void StartLowEnergyDiscoverySessionExpectedToFail();

  // Check if Low Energy is available. On Mac, we require OS X >= 10.10.
  virtual bool PlatformSupportsLowEnergy() = 0;

  // Initializes the BluetoothAdapter |adapter_| with the system adapter.
  virtual void InitWithDefaultAdapter() {}

  // Initializes the BluetoothAdapter |adapter_| with the system adapter forced
  // to be ignored as if it did not exist. This enables tests for when an
  // adapter is not present on the system.
  virtual void InitWithoutDefaultAdapter() {}

  // Initializes the BluetoothAdapter |adapter_| with a fake adapter that can be
  // controlled by this test fixture.
  virtual void InitWithFakeAdapter() {}

  // Configures the fake adapter to lack the necessary permissions to scan for
  // devices.  Returns false if the current platform always has permission.
  virtual bool DenyPermission();

  // Create a fake Low Energy device and discover it.
  // |device_ordinal| selects between multiple fake device data sets to produce:
  //   1: kTestDeviceName with advertised UUIDs kTestUUIDGenericAccess,
  //      kTestUUIDGenericAttribute and address kTestDeviceAddress1.
  //   2: kTestDeviceName with advertised UUIDs kTestUUIDImmediateAlert,
  //      kTestUUIDLinkLoss and address kTestDeviceAddress1.
  //   3: kTestDeviceNameEmpty with no advertised UUIDs and address
  //      kTestDeviceAddress1.
  //   4: kTestDeviceNameEmpty with no advertised UUIDs and address
  //      kTestDeviceAddress2.
  //   5: Device with no name, with no advertised UUIDs and address
  //      kTestDeviceAddress1.
  virtual BluetoothDevice* SimulateLowEnergyDevice(int device_ordinal);

  // Simulates success of implementation details of CreateGattConnection.
  virtual void SimulateGattConnection(BluetoothDevice* device) {}

  // Simulates failure of CreateGattConnection with the given error code.
  virtual void SimulateGattConnectionError(BluetoothDevice* device,
                                           BluetoothDevice::ConnectErrorCode) {}

  // Simulates GattConnection disconnecting.
  virtual void SimulateGattDisconnection(BluetoothDevice* device) {}

  // Simulates success of discovering services. |uuids| is used to create a
  // service for each UUID string. Multiple UUIDs with the same value produce
  // multiple service instances.
  virtual void SimulateGattServicesDiscovered(
      BluetoothDevice* device,
      const std::vector<std::string>& uuids) {}

  // Simulates remove of a |service|.
  virtual void SimulateGattServiceRemoved(BluetoothRemoteGattService* service) {
  }

  // Simulates failure to discover services.
  virtual void SimulateGattServicesDiscoveryError(BluetoothDevice* device) {}

  // Simulates a Characteristic on a service.
  virtual void SimulateGattCharacteristic(BluetoothRemoteGattService* service,
                                          const std::string& uuid,
                                          int properties) {}

  // Simulates remove of a |characteristic| from |service|.
  virtual void SimulateGattCharacteristicRemoved(
      BluetoothRemoteGattService* service,
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Remembers |characteristic|'s platform specific object to be used in a
  // subsequent call to methods such as SimulateGattCharacteristicRead that
  // accept a nullptr value to select this remembered characteristic. This
  // enables tests where the platform attempts to reference characteristic
  // objects after the Chrome objects have been deleted, e.g. with DeleteDevice.
  virtual void RememberCharacteristicForSubsequentAction(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Remembers |characteristic|'s Client Characteristic Configuration (CCC)
  // descriptor's platform specific object to be used in a subsequent call to
  // methods such as SimulateGattNotifySessionStarted. This enables tests where
  // the platform attempts to reference descriptor objects after the Chrome
  // objects have been deleted, e.g. with DeleteDevice.
  virtual void RememberCCCDescriptorForSubsequentAction(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Characteristic Set Notify success.
  // If |characteristic| is null, acts upon the characteristic & CCC
  // descriptor provided to RememberCharacteristicForSubsequentAction &
  // RememberCCCDescriptorForSubsequentAction.
  virtual void SimulateGattNotifySessionStarted(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Characteristic Set Notify error.
  // If |characteristic| is null, acts upon the characteristic & CCC
  // descriptor provided to RememberCharacteristicForSubsequentAction &
  // RememberCCCDescriptorForSubsequentAction.
  virtual void SimulateGattNotifySessionStartError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothRemoteGattService::GattErrorCode error_code) {}

  // Simulates a Characteristic Set Notify operation failing synchronously once
  // for an unknown reason.
  virtual void SimulateGattCharacteristicSetNotifyWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Characteristic Changed operation with updated |value|.
  virtual void SimulateGattCharacteristicChanged(
      BluetoothRemoteGattCharacteristic* characteristic,
      const std::vector<uint8_t>& value) {}

  // Simulates a Characteristic Read operation succeeding, returning |value|.
  // If |characteristic| is null, acts upon the characteristic provided to
  // RememberCharacteristicForSubsequentAction.
  virtual void SimulateGattCharacteristicRead(
      BluetoothRemoteGattCharacteristic* characteristic,
      const std::vector<uint8_t>& value) {}

  // Simulates a Characteristic Read operation failing with a GattErrorCode.
  virtual void SimulateGattCharacteristicReadError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothRemoteGattService::GattErrorCode) {}

  // Simulates a Characteristic Read operation failing synchronously once for an
  // unknown reason.
  virtual void SimulateGattCharacteristicReadWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Characteristic Write operation succeeding, returning |value|.
  // If |characteristic| is null, acts upon the characteristic provided to
  // RememberCharacteristicForSubsequentAction.
  virtual void SimulateGattCharacteristicWrite(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Characteristic Write operation failing with a GattErrorCode.
  virtual void SimulateGattCharacteristicWriteError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothRemoteGattService::GattErrorCode) {}

  // Simulates a Characteristic Write operation failing synchronously once for
  // an unknown reason.
  virtual void SimulateGattCharacteristicWriteWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) {}

  // Simulates a Descriptor on a service.
  virtual void SimulateGattDescriptor(
      BluetoothRemoteGattCharacteristic* characteristic,
      const std::string& uuid) {}

  // Simulates reading a value from a locally hosted GATT characteristic by a
  // remote central device. Returns the value that was read from the local
  // GATT characteristic in the value callback.
  virtual void SimulateLocalGattCharacteristicValueReadRequest(
      BluetoothLocalGattService* service,
      BluetoothLocalGattCharacteristic* characteristic,
      const BluetoothLocalGattService::Delegate::ValueCallback& value_callback,
      const base::Closure& error_callback) {}

  // Simulates write a value to a locally hosted GATT characteristic by a
  // remote central device.
  virtual void SimulateLocalGattCharacteristicValueWriteRequest(
      BluetoothLocalGattService* service,
      BluetoothLocalGattCharacteristic* characteristic,
      const std::vector<uint8_t>& value_to_write,
      const base::Closure& success_callback,
      const base::Closure& error_callback) {}

  // Simulates reading a value from a locally hosted GATT descriptor by a
  // remote central device. Returns the value that was read from the local
  // GATT descriptor in the value callback.
  virtual void SimulateLocalGattDescriptorValueReadRequest(
      BluetoothLocalGattService* service,
      BluetoothLocalGattDescriptor* descriptor,
      const BluetoothLocalGattService::Delegate::ValueCallback& value_callback,
      const base::Closure& error_callback) {}

  // Simulates write a value to a locally hosted GATT descriptor by a
  // remote central device.
  virtual void SimulateLocalGattDescriptorValueWriteRequest(
      BluetoothLocalGattService* service,
      BluetoothLocalGattDescriptor* descriptor,
      const std::vector<uint8_t>& value_to_write,
      const base::Closure& success_callback,
      const base::Closure& error_callback) {}

  // Simulates starting or stopping a notification session for a locally
  // hosted GATT characteristic by a remote device. Returns false if we were
  // not able to start or stop notifications.
  virtual bool SimulateLocalGattCharacteristicNotificationsRequest(
      BluetoothLocalGattService* service,
      BluetoothLocalGattCharacteristic* characteristic,
      bool start);

  // Returns the value for the last notification that was sent on this
  // characteristic.
  virtual std::vector<uint8_t> LastNotifactionValueForCharacteristic(
      BluetoothLocalGattCharacteristic* characteristic);

  // Remembers |descriptor|'s platform specific object to be used in a
  // subsequent call to methods such as SimulateGattDescriptorRead that
  // accept a nullptr value to select this remembered descriptor. This
  // enables tests where the platform attempts to reference descriptor
  // objects after the Chrome objects have been deleted, e.g. with DeleteDevice.
  virtual void RememberDescriptorForSubsequentAction(
      BluetoothRemoteGattDescriptor* descriptor) {}

  // Simulates a Descriptor Read operation succeeding, returning |value|.
  // If |descriptor| is null, acts upon the descriptor provided to
  // RememberDescriptorForSubsequentAction.
  virtual void SimulateGattDescriptorRead(
      BluetoothRemoteGattDescriptor* descriptor,
      const std::vector<uint8_t>& value) {}

  // Simulates a Descriptor Read operation failing with a GattErrorCode.
  virtual void SimulateGattDescriptorReadError(
      BluetoothRemoteGattDescriptor* descriptor,
      BluetoothRemoteGattService::GattErrorCode) {}

  // Simulates a Descriptor Read operation failing synchronously once for an
  // unknown reason.
  virtual void SimulateGattDescriptorReadWillFailSynchronouslyOnce(
      BluetoothRemoteGattDescriptor* descriptor) {}

  // Simulates a Descriptor Write operation succeeding, returning |value|.
  // If |descriptor| is null, acts upon the descriptor provided to
  // RememberDescriptorForSubsequentAction.
  virtual void SimulateGattDescriptorWrite(
      BluetoothRemoteGattDescriptor* descriptor) {}

  // Simulates a Descriptor Write operation failing with a GattErrorCode.
  virtual void SimulateGattDescriptorWriteError(
      BluetoothRemoteGattDescriptor* descriptor,
      BluetoothRemoteGattService::GattErrorCode) {}

  // Simulates a Descriptor Write operation failing synchronously once for
  // an unknown reason.
  virtual void SimulateGattDescriptorWriteWillFailSynchronouslyOnce(
      BluetoothRemoteGattDescriptor* descriptor) {}

  // Returns a list of local GATT services registered with the adapter.
  virtual std::vector<BluetoothLocalGattService*> RegisteredGattServices();

  // Removes the device from the adapter and deletes it.
  virtual void DeleteDevice(BluetoothDevice* device);

  // Callbacks that increment |callback_count_|, |error_callback_count_|:
  void Callback(Call expected);
  void DiscoverySessionCallback(Call expected,
                                std::unique_ptr<BluetoothDiscoverySession>);
  void GattConnectionCallback(Call expected,
                              std::unique_ptr<BluetoothGattConnection>);
  void NotifyCallback(Call expected,
                      std::unique_ptr<BluetoothGattNotifySession>);
  void ReadValueCallback(Call expected, const std::vector<uint8_t>& value);
  void ErrorCallback(Call expected);
  void ConnectErrorCallback(Call expected,
                            enum BluetoothDevice::ConnectErrorCode);
  void GattErrorCallback(Call expected,
                         BluetoothRemoteGattService::GattErrorCode);
  void ReentrantStartNotifySessionSuccessCallback(
      Call expected,
      BluetoothRemoteGattCharacteristic* characteristic,
      std::unique_ptr<BluetoothGattNotifySession> notify_session);
  void ReentrantStartNotifySessionErrorCallback(
      Call expected,
      BluetoothRemoteGattCharacteristic* characteristic,
      bool error_in_reentrant,
      BluetoothGattService::GattErrorCode error_code);

  // Accessors to get callbacks bound to this fixture:
  base::Closure GetCallback(Call expected);
  BluetoothAdapter::DiscoverySessionCallback GetDiscoverySessionCallback(
      Call expected);
  BluetoothDevice::GattConnectionCallback GetGattConnectionCallback(
      Call expected);
  BluetoothRemoteGattCharacteristic::NotifySessionCallback GetNotifyCallback(
      Call expected);
  BluetoothRemoteGattCharacteristic::ValueCallback GetReadValueCallback(
      Call expected);
  BluetoothAdapter::ErrorCallback GetErrorCallback(Call expected);
  BluetoothDevice::ConnectErrorCallback GetConnectErrorCallback(Call expected);
  base::Callback<void(BluetoothRemoteGattService::GattErrorCode)>
  GetGattErrorCallback(Call expected);
  BluetoothRemoteGattCharacteristic::NotifySessionCallback
  GetReentrantStartNotifySessionSuccessCallback(
      Call expected,
      BluetoothRemoteGattCharacteristic* characteristic);
  base::Callback<void(BluetoothGattService::GattErrorCode)>
  GetReentrantStartNotifySessionErrorCallback(
      Call expected,
      BluetoothRemoteGattCharacteristic* characteristic,
      bool error_in_reentrant);

  // Reset all event count members to 0.
  void ResetEventCounts();

  // A Message loop is required by some implementations that will PostTasks and
  // by base::RunLoop().RunUntilIdle() use in this fixture.
  base::MessageLoop message_loop_;

  scoped_refptr<BluetoothAdapter> adapter_;
  ScopedVector<BluetoothDiscoverySession> discovery_sessions_;
  ScopedVector<BluetoothGattConnection> gatt_connections_;
  enum BluetoothDevice::ConnectErrorCode last_connect_error_code_ =
      BluetoothDevice::ERROR_UNKNOWN;
  ScopedVector<BluetoothGattNotifySession> notify_sessions_;
  std::vector<uint8_t> last_read_value_;
  std::vector<uint8_t> last_write_value_;
  BluetoothRemoteGattService::GattErrorCode last_gatt_error_code_;

  int callback_count_ = 0;
  int error_callback_count_ = 0;
  int gatt_connection_attempts_ = 0;
  int gatt_disconnection_attempts_ = 0;
  int gatt_discovery_attempts_ = 0;
  int gatt_notify_characteristic_attempts_ = 0;
  int gatt_read_characteristic_attempts_ = 0;
  int gatt_write_characteristic_attempts_ = 0;
  int gatt_read_descriptor_attempts_ = 0;
  int gatt_write_descriptor_attempts_ = 0;

  // The following values are used to make sure the correct callbacks
  // have been called. They are not reset when calling ResetEventCounts().
  int expected_success_callback_calls_ = 0;
  int expected_error_callback_calls_ = 0;
  int actual_success_callback_calls_ = 0;
  int actual_error_callback_calls_ = 0;
  bool unexpected_success_callback_ = false;
  bool unexpected_error_callback_ = false;

  base::WeakPtrFactory<BluetoothTestBase> weak_factory_;
};

}  // namespace device

#endif  // DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_H_
