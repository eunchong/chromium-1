// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_DESCRIPTOR_SERVICE_PROVIDER_IMPL_H_
#define DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_DESCRIPTOR_SERVICE_PROVIDER_IMPL_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/platform_thread.h"
#include "dbus/bus.h"
#include "dbus/exported_object.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "device/bluetooth/dbus/bluetooth_gatt_attribute_value_delegate.h"
#include "device/bluetooth/dbus/bluetooth_gatt_descriptor_service_provider.h"

namespace bluez {

// The BluetoothGattDescriptorServiceProvider implementation used in production.
class DEVICE_BLUETOOTH_EXPORT BluetoothGattDescriptorServiceProviderImpl
    : public BluetoothGattDescriptorServiceProvider {
 public:
  // Use nullptr for |bus| to create for testing.
  BluetoothGattDescriptorServiceProviderImpl(
      dbus::Bus* bus,
      const dbus::ObjectPath& object_path,
      std::unique_ptr<BluetoothGattAttributeValueDelegate> delegate,
      const std::string& uuid,
      const std::vector<std::string>& flags,
      const dbus::ObjectPath& characteristic_path);
  ~BluetoothGattDescriptorServiceProviderImpl() override;

  // BluetoothGattDescriptorServiceProvider override.
  void SendValueChanged(const std::vector<uint8_t>& value) override;

 private:
  // Returns true if the current thread is on the origin thread.
  bool OnOriginThread();

  // Called by dbus:: when the Bluetooth daemon fetches a single property of
  // the descriptor.
  void Get(dbus::MethodCall* method_call,
           dbus::ExportedObject::ResponseSender response_sender);

  // Called by dbus:: when the Bluetooth daemon sets a single property of the
  // descriptor.
  void Set(dbus::MethodCall* method_call,
           dbus::ExportedObject::ResponseSender response_sender);

  // Called by dbus:: when the Bluetooth daemon fetches all properties of the
  // descriptor.
  void GetAll(dbus::MethodCall* method_call,
              dbus::ExportedObject::ResponseSender response_sender);

  // Called by dbus:: when a method is exported.
  void OnExported(const std::string& interface_name,
                  const std::string& method_name,
                  bool success);

  // Called by the Delegate in response to a method to call to get all
  // properties, in which the delegate has successfully returned the
  // descriptor value.
  void OnGetAll(dbus::MethodCall* method_call,
                dbus::ExportedObject::ResponseSender response_sender,
                const std::vector<uint8_t>& value);

  // Writes the characteristics's properties into the provided writer. If
  // value is not null, it is written also, otherwise no value property is
  // written.
  void WriteProperties(dbus::MessageWriter* writer,
                       const std::vector<uint8_t>* value) override;

  // Called by the Delegate in response to a successful method call to get the
  // descriptor value.
  void OnGet(dbus::MethodCall* method_call,
             dbus::ExportedObject::ResponseSender response_sender,
             const std::vector<uint8_t>& value);

  // Called by the Delegate in response to a successful method call to set the
  // descriptor value.
  void OnSet(dbus::MethodCall* method_call,
             dbus::ExportedObject::ResponseSender response_sender);

  // Called by the Delegate in response to a failed method call to get or set
  // the descriptor value.
  void OnFailure(dbus::MethodCall* method_call,
                 dbus::ExportedObject::ResponseSender response_sender);

  const dbus::ObjectPath& object_path() const override;

  // Origin thread (i.e. the UI thread in production).
  base::PlatformThreadId origin_thread_id_;

  // 128-bit descriptor UUID of this object.
  std::string uuid_;

  // Permissions for this descriptor.
  std::vector<std::string> flags_;

  // D-Bus bus object is exported on, not owned by this object and must
  // outlive it.
  dbus::Bus* bus_;

  // Incoming methods to get and set the "Value" property are passed on to the
  // delegate and callbacks passed to generate a reply. |delegate_| is generally
  // the object that owns this one and must outlive it.
  std::unique_ptr<BluetoothGattAttributeValueDelegate> delegate_;

  // D-Bus object path of object we are exporting, kept so we can unregister
  // again in our destructor.
  dbus::ObjectPath object_path_;

  // Object path of the GATT characteristic that the exported descriptor belongs
  // to.
  dbus::ObjectPath characteristic_path_;

  // D-Bus object we are exporting, owned by this object.
  scoped_refptr<dbus::ExportedObject> exported_object_;

  // Weak pointer factory for generating 'this' pointers that might live longer
  // than we do.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothGattDescriptorServiceProviderImpl>
      weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothGattDescriptorServiceProviderImpl);
};

}  // namespace bluez

#endif  // DEVICE_BLUETOOTH_DBUS_BLUETOOTH_GATT_DESCRIPTOR_SERVICE_PROVIDER_IMPL_H_
