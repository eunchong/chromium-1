// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_LOGGING_NETWORK_CHANGE_OBSERVER_H_
#define NET_BASE_LOGGING_NETWORK_CHANGE_OBSERVER_H_

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace net {

class NetLog;

// A class that adds NetLog events for network change events coming from the
// net::NetworkChangeNotifier.
class NET_EXPORT LoggingNetworkChangeObserver
    : public NetworkChangeNotifier::IPAddressObserver,
      public NetworkChangeNotifier::ConnectionTypeObserver,
      public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  // Note: |net_log| must remain valid throughout the lifetime of this
  // LoggingNetworkChangeObserver.
  explicit LoggingNetworkChangeObserver(NetLog* net_log);
  ~LoggingNetworkChangeObserver() override;

 private:
  // NetworkChangeNotifier::IPAddressObserver implementation.
  void OnIPAddressChanged() override;

  // NetworkChangeNotifier::ConnectionTypeObserver implementation.
  void OnConnectionTypeChanged(
      NetworkChangeNotifier::ConnectionType type) override;

  // NetworkChangeNotifier::NetworkChangeObserver implementation.
  void OnNetworkChanged(NetworkChangeNotifier::ConnectionType type) override;

 private:
  NetLog* net_log_;

  DISALLOW_COPY_AND_ASSIGN(LoggingNetworkChangeObserver);
};

}  // namespace net

#endif  // NET_BASE_LOGGING_NETWORK_CHANGE_OBSERVER_H_