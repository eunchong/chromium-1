// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/weak_ptr.h"
#include "device/bluetooth/bluetooth_local_gatt_characteristic.h"
#include "device/bluetooth/test/bluetooth_gatt_server_test.h"
#include "device/bluetooth/test/bluetooth_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace device {

class BluetoothLocalGattCharacteristicTest : public BluetoothGattServerTest {
 public:
  void SetUp() override {
    BluetoothGattServerTest::SetUp();

    StartGattSetup();
    read_characteristic_ = BluetoothLocalGattCharacteristic::Create(
        BluetoothUUID(kTestUUIDGenericAttribute),
        device::BluetoothLocalGattCharacteristic::
            PROPERTY_READ_ENCRYPTED_AUTHENTICATED,
        device::BluetoothLocalGattCharacteristic::Permissions(),
        service_.get());
    write_characteristic_ = BluetoothLocalGattCharacteristic::Create(
        BluetoothUUID(kTestUUIDGenericAttribute),
        device::BluetoothLocalGattCharacteristic::PROPERTY_RELIABLE_WRITE,
        device::BluetoothLocalGattCharacteristic::Permissions(),
        service_.get());
    notify_characteristic_ = BluetoothLocalGattCharacteristic::Create(
        BluetoothUUID(kTestUUIDGenericAttribute),
        device::BluetoothLocalGattCharacteristic::PROPERTY_NOTIFY,
        device::BluetoothLocalGattCharacteristic::Permissions(),
        service_.get());
    indicate_characteristic_ = BluetoothLocalGattCharacteristic::Create(
        BluetoothUUID(kTestUUIDGenericAttribute),
        device::BluetoothLocalGattCharacteristic::PROPERTY_INDICATE,
        device::BluetoothLocalGattCharacteristic::Permissions(),
        service_.get());
    EXPECT_LT(0u, read_characteristic_->GetIdentifier().size());
    EXPECT_LT(0u, write_characteristic_->GetIdentifier().size());
    EXPECT_LT(0u, notify_characteristic_->GetIdentifier().size());
    CompleteGattSetup();
  }

 protected:
  base::WeakPtr<BluetoothLocalGattCharacteristic> read_characteristic_;
  base::WeakPtr<BluetoothLocalGattCharacteristic> write_characteristic_;
  base::WeakPtr<BluetoothLocalGattCharacteristic> notify_characteristic_;
  base::WeakPtr<BluetoothLocalGattCharacteristic> indicate_characteristic_;
};

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, ReadLocalCharacteristicValue) {
  delegate_->value_to_write_ = 0x1337;
  SimulateLocalGattCharacteristicValueReadRequest(
      service_.get(), read_characteristic_.get(),
      GetReadValueCallback(Call::EXPECTED), GetCallback(Call::NOT_EXPECTED));

  EXPECT_EQ(delegate_->value_to_write_, GetInteger(last_read_value_));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, WriteLocalCharacteristicValue) {
  const uint64_t kValueToWrite = 0x7331ul;
  SimulateLocalGattCharacteristicValueWriteRequest(
      service_.get(), write_characteristic_.get(), GetValue(kValueToWrite),
      GetCallback(Call::EXPECTED), GetCallback(Call::NOT_EXPECTED));

  EXPECT_EQ(kValueToWrite, delegate_->last_written_value_);
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, ReadLocalCharacteristicValueFail) {
  delegate_->value_to_write_ = 0x1337;
  delegate_->should_fail_ = true;
  SimulateLocalGattCharacteristicValueReadRequest(
      service_.get(), read_characteristic_.get(),
      GetReadValueCallback(Call::NOT_EXPECTED), GetCallback(Call::EXPECTED));

  EXPECT_NE(delegate_->value_to_write_, GetInteger(last_read_value_));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest,
       ReadLocalCharacteristicValueWrongPermission) {
  delegate_->value_to_write_ = 0x1337;
  SimulateLocalGattCharacteristicValueReadRequest(
      service_.get(), write_characteristic_.get(),
      GetReadValueCallback(Call::NOT_EXPECTED), GetCallback(Call::EXPECTED));

  EXPECT_NE(delegate_->value_to_write_, GetInteger(last_read_value_));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest,
       WriteLocalCharacteristicValueFail) {
  const uint64_t kValueToWrite = 0x7331ul;
  delegate_->should_fail_ = true;
  SimulateLocalGattCharacteristicValueWriteRequest(
      service_.get(), write_characteristic_.get(), GetValue(kValueToWrite),
      GetCallback(Call::NOT_EXPECTED), GetCallback(Call::EXPECTED));

  EXPECT_NE(kValueToWrite, delegate_->last_written_value_);
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest,
       WriteLocalCharacteristicValueWrongPermission) {
  const uint64_t kValueToWrite = 0x7331ul;
  SimulateLocalGattCharacteristicValueWriteRequest(
      service_.get(), read_characteristic_.get(), GetValue(kValueToWrite),
      GetCallback(Call::NOT_EXPECTED), GetCallback(Call::EXPECTED));

  EXPECT_NE(kValueToWrite, delegate_->last_written_value_);
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, StartAndStopNotifications) {
  EXPECT_FALSE(SimulateLocalGattCharacteristicNotificationsRequest(
      service_.get(), read_characteristic_.get(), true));
  EXPECT_FALSE(delegate_->NotificationStatusForCharacteristic(
      read_characteristic_.get()));

  EXPECT_FALSE(SimulateLocalGattCharacteristicNotificationsRequest(
      service_.get(), write_characteristic_.get(), true));
  EXPECT_FALSE(delegate_->NotificationStatusForCharacteristic(
      write_characteristic_.get()));

  EXPECT_TRUE(SimulateLocalGattCharacteristicNotificationsRequest(
      service_.get(), notify_characteristic_.get(), true));
  EXPECT_TRUE(delegate_->NotificationStatusForCharacteristic(
      notify_characteristic_.get()));

  EXPECT_TRUE(SimulateLocalGattCharacteristicNotificationsRequest(
      service_.get(), notify_characteristic_.get(), false));
  EXPECT_FALSE(delegate_->NotificationStatusForCharacteristic(
      notify_characteristic_.get()));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, SendNotifications) {
  const uint64_t kNotifyValue = 0x7331ul;
  EXPECT_EQ(BluetoothLocalGattCharacteristic::NOTIFICATION_SUCCESS,
            notify_characteristic_->NotifyValueChanged(GetValue(kNotifyValue),
                                                       false));
  EXPECT_EQ(kNotifyValue, GetInteger(LastNotifactionValueForCharacteristic(
                              notify_characteristic_.get())));

  const uint64_t kIndicateValue = 0x1337ul;
  EXPECT_EQ(BluetoothLocalGattCharacteristic::NOTIFICATION_SUCCESS,
            indicate_characteristic_->NotifyValueChanged(
                GetValue(kIndicateValue), true));
  EXPECT_EQ(kIndicateValue, GetInteger(LastNotifactionValueForCharacteristic(
                                indicate_characteristic_.get())));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest, SendNotificationsWrongProperties) {
  const uint64_t kNewValue = 0x3334ul;
  EXPECT_EQ(
      BluetoothLocalGattCharacteristic::NOTIFY_PROPERTY_NOT_SET,
      read_characteristic_->NotifyValueChanged(GetValue(kNewValue), false));
  EXPECT_NE(kNewValue, GetInteger(LastNotifactionValueForCharacteristic(
                           read_characteristic_.get())));

  EXPECT_EQ(
      BluetoothLocalGattCharacteristic::NOTIFY_PROPERTY_NOT_SET,
      write_characteristic_->NotifyValueChanged(GetValue(kNewValue), false));
  EXPECT_NE(kNewValue, GetInteger(LastNotifactionValueForCharacteristic(
                           write_characteristic_.get())));

  const uint64_t kNotifyValue = 0x7331ul;
  EXPECT_EQ(
      BluetoothLocalGattCharacteristic::INDICATE_PROPERTY_NOT_SET,
      notify_characteristic_->NotifyValueChanged(GetValue(kNotifyValue), true));
  EXPECT_NE(kNotifyValue, GetInteger(LastNotifactionValueForCharacteristic(
                              notify_characteristic_.get())));

  const uint64_t kIndicateValue = 0x1337ul;
  EXPECT_EQ(BluetoothLocalGattCharacteristic::NOTIFY_PROPERTY_NOT_SET,
            indicate_characteristic_->NotifyValueChanged(
                GetValue(kIndicateValue), false));
  EXPECT_NE(kIndicateValue, GetInteger(LastNotifactionValueForCharacteristic(
                                indicate_characteristic_.get())));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
TEST_F(BluetoothLocalGattCharacteristicTest,
       SendNotificationsServiceNotRegistered) {
  service_->Unregister(GetCallback(Call::EXPECTED),
                       GetGattErrorCallback(Call::NOT_EXPECTED));
  const uint64_t kNotifyValue = 0x7331ul;
  EXPECT_EQ(BluetoothLocalGattCharacteristic::SERVICE_NOT_REGISTERED,
            notify_characteristic_->NotifyValueChanged(GetValue(kNotifyValue),
                                                       false));
  EXPECT_NE(kNotifyValue, GetInteger(LastNotifactionValueForCharacteristic(
                              notify_characteristic_.get())));
}
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX)

}  // namespace device
