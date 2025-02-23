// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/net/arc_net_host_impl.h"

#include <memory>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chromeos/login/login_state.h"
#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_type_pattern.h"
#include "chromeos/network/network_util.h"
#include "chromeos/network/onc/onc_utils.h"
#include "components/arc/arc_bridge_service.h"

namespace {

const int kGetNetworksListLimit = 100;

}  // namespace

namespace arc {

chromeos::NetworkStateHandler* GetStateHandler() {
  return chromeos::NetworkHandler::Get()->network_state_handler();
}

chromeos::ManagedNetworkConfigurationHandler* GetManagedConfigurationHandler() {
  return chromeos::NetworkHandler::Get()
      ->managed_network_configuration_handler();
}

chromeos::NetworkConnectionHandler* GetNetworkConnectionHandler() {
  return chromeos::NetworkHandler::Get()->network_connection_handler();
}

bool IsDeviceOwner() {
  // Check whether the logged-in Chrome OS user is allowed to add or
  // remove WiFi networks.
  return chromeos::LoginState::Get()->GetLoggedInUserType() ==
         chromeos::LoginState::LOGGED_IN_USER_OWNER;
}

ArcNetHostImpl::ArcNetHostImpl(ArcBridgeService* bridge_service)
    : ArcService(bridge_service), binding_(this) {
  arc_bridge_service()->AddObserver(this);
  GetStateHandler()->AddObserver(this, FROM_HERE);
}

ArcNetHostImpl::~ArcNetHostImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
  arc_bridge_service()->RemoveObserver(this);
  if (chromeos::NetworkHandler::IsInitialized()) {
    GetStateHandler()->RemoveObserver(this, FROM_HERE);
  }
}

void ArcNetHostImpl::OnNetInstanceReady() {
  DCHECK(thread_checker_.CalledOnValidThread());

  mojom::NetHostPtr host;
  binding_.Bind(GetProxy(&host));
  arc_bridge_service()->net_instance()->Init(std::move(host));
}

void ArcNetHostImpl::GetNetworksDeprecated(
    bool configured_only,
    bool visible_only,
    const GetNetworksDeprecatedCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (configured_only && visible_only) {
    VLOG(1) << "Illegal arguments - both configured and visible networks "
               "requested.";
    return;
  }

  mojom::GetNetworksRequestType type =
      mojom::GetNetworksRequestType::CONFIGURED_ONLY;
  if (visible_only) {
    type = mojom::GetNetworksRequestType::VISIBLE_ONLY;
  }

  GetNetworks(type, callback);
}

void ArcNetHostImpl::GetNetworks(mojom::GetNetworksRequestType type,
                                 const GetNetworksCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  mojom::NetworkDataPtr data = mojom::NetworkData::New();
  bool configured_only = true;
  bool visible_only = false;
  if (type == mojom::GetNetworksRequestType::VISIBLE_ONLY) {
    configured_only = false;
    visible_only = true;
  }

  // Retrieve list of nearby wifi networks
  chromeos::NetworkTypePattern network_pattern =
      chromeos::onc::NetworkTypePatternFromOncType(onc::network_type::kWiFi);
  std::unique_ptr<base::ListValue> network_properties_list =
      chromeos::network_util::TranslateNetworkListToONC(
          network_pattern, configured_only, visible_only,
          kGetNetworksListLimit);

  // Extract info for each network and add it to the list.
  // Even if there's no WiFi, an empty (size=0) list must be returned and not a
  // null one. The explicitly sized New() constructor ensures the non-null
  // property.
  mojo::Array<mojom::WifiConfigurationPtr> networks =
      mojo::Array<mojom::WifiConfigurationPtr>::New(0);
  for (base::Value* value : *network_properties_list) {
    mojom::WifiConfigurationPtr wc = mojom::WifiConfiguration::New();

    base::DictionaryValue* network_dict = nullptr;
    value->GetAsDictionary(&network_dict);
    DCHECK(network_dict);

    // kName is a post-processed version of kHexSSID.
    std::string tmp;
    network_dict->GetString(onc::network_config::kName, &tmp);
    DCHECK(!tmp.empty());
    wc->ssid = tmp;

    tmp.clear();
    network_dict->GetString(onc::network_config::kGUID, &tmp);
    DCHECK(!tmp.empty());
    wc->guid = tmp;

    base::DictionaryValue* wifi_dict = nullptr;
    network_dict->GetDictionary(onc::network_config::kWiFi, &wifi_dict);
    DCHECK(wifi_dict);

    if (!wifi_dict->GetInteger(onc::wifi::kFrequency, &wc->frequency))
      wc->frequency = 0;
    if (!wifi_dict->GetInteger(onc::wifi::kSignalStrength,
                               &wc->signal_strength))
      wc->signal_strength = 0;

    if (!wifi_dict->GetString(onc::wifi::kSecurity, &tmp))
      NOTREACHED();
    DCHECK(!tmp.empty());
    wc->security = tmp;

    if (!wifi_dict->GetString(onc::wifi::kBSSID, &tmp))
      NOTREACHED();
    DCHECK(!tmp.empty());
    wc->bssid = tmp;

    mojom::VisibleNetworkDetailsPtr details =
        mojom::VisibleNetworkDetails::New();
    details->frequency = wc->frequency;
    details->signal_strength = wc->signal_strength;
    details->bssid = wc->bssid;
    wc->details = mojom::NetworkDetails::New();
    wc->details->set_visible(std::move(details));

    networks.push_back(std::move(wc));
  }
  data->networks = std::move(networks);
  callback.Run(std::move(data));
}

void CreateNetworkSuccessCallback(
    const arc::mojom::NetHost::CreateNetworkCallback& mojo_callback,
    const std::string& service_path,
    const std::string& guid) {
  VLOG(1) << "CreateNetworkSuccessCallback";
  mojo_callback.Run(guid);
}

void CreateNetworkFailureCallback(
    const arc::mojom::NetHost::CreateNetworkCallback& mojo_callback,
    const std::string& error_name,
    std::unique_ptr<base::DictionaryValue> error_data) {
  VLOG(1) << "CreateNetworkFailureCallback: " << error_name;
  mojo_callback.Run("");
}

void ArcNetHostImpl::CreateNetwork(mojom::WifiConfigurationPtr cfg,
                                   const CreateNetworkCallback& callback) {
  if (!IsDeviceOwner()) {
    callback.Run("");
    return;
  }

  std::unique_ptr<base::DictionaryValue> properties(new base::DictionaryValue);
  std::unique_ptr<base::DictionaryValue> wifi_dict(new base::DictionaryValue);

  if (cfg->hexssid.is_null() || !cfg->details) {
    callback.Run("");
    return;
  }
  mojom::ConfiguredNetworkDetailsPtr details =
      std::move(cfg->details->get_configured());
  if (!details) {
    callback.Run("");
    return;
  }

  properties->SetStringWithoutPathExpansion(onc::network_config::kType,
                                            onc::network_config::kWiFi);
  wifi_dict->SetStringWithoutPathExpansion(onc::wifi::kHexSSID, cfg->hexssid);
  wifi_dict->SetBooleanWithoutPathExpansion(onc::wifi::kAutoConnect,
                                            details->autoconnect);
  if (cfg->security.get().empty()) {
    wifi_dict->SetStringWithoutPathExpansion(onc::wifi::kSecurity,
                                             onc::wifi::kSecurityNone);
  } else {
    wifi_dict->SetStringWithoutPathExpansion(onc::wifi::kSecurity,
                                             cfg->security);
    if (!details->passphrase.is_null()) {
      wifi_dict->SetStringWithoutPathExpansion(onc::wifi::kPassphrase,
                                               details->passphrase);
    }
  }
  properties->SetWithoutPathExpansion(onc::network_config::kWiFi,
                                      std::move(wifi_dict));

  std::string user_id_hash = chromeos::LoginState::Get()->primary_user_hash();
  GetManagedConfigurationHandler()->CreateConfiguration(
      user_id_hash, *properties,
      base::Bind(&CreateNetworkSuccessCallback, callback),
      base::Bind(&CreateNetworkFailureCallback, callback));
}

void ForgetNetworkSuccessCallback(
    const arc::mojom::NetHost::ForgetNetworkCallback& mojo_callback) {
  mojo_callback.Run(mojom::NetworkResult::SUCCESS);
}

void ForgetNetworkFailureCallback(
    const arc::mojom::NetHost::ForgetNetworkCallback& mojo_callback,
    const std::string& error_name,
    std::unique_ptr<base::DictionaryValue> error_data) {
  VLOG(1) << "ForgetNetworkFailureCallback: " << error_name;
  mojo_callback.Run(mojom::NetworkResult::FAILURE);
}

void ArcNetHostImpl::ForgetNetwork(const mojo::String& guid,
                                   const ForgetNetworkCallback& callback) {
  if (!IsDeviceOwner()) {
    callback.Run(mojom::NetworkResult::FAILURE);
    return;
  }

  const chromeos::NetworkState* network =
      GetStateHandler()->GetNetworkStateFromGuid(guid);

  if (!network) {
    callback.Run(mojom::NetworkResult::FAILURE);
    return;
  }

  GetManagedConfigurationHandler()->RemoveConfiguration(
      network->path(), base::Bind(&ForgetNetworkSuccessCallback, callback),
      base::Bind(&ForgetNetworkFailureCallback, callback));
}

void StartConnectSuccessCallback(
    const arc::mojom::NetHost::StartConnectCallback& mojo_callback) {
  mojo_callback.Run(mojom::NetworkResult::SUCCESS);
}

void StartConnectFailureCallback(
    const arc::mojom::NetHost::StartConnectCallback& mojo_callback,
    const std::string& error_name,
    std::unique_ptr<base::DictionaryValue> error_data) {
  VLOG(1) << "StartConnectFailureCallback: " << error_name;
  mojo_callback.Run(mojom::NetworkResult::FAILURE);
}

void ArcNetHostImpl::StartConnect(const mojo::String& guid,
                                  const StartConnectCallback& callback) {
  const chromeos::NetworkState* network =
      GetStateHandler()->GetNetworkStateFromGuid(guid);

  if (!network) {
    callback.Run(mojom::NetworkResult::FAILURE);
    return;
  }

  GetNetworkConnectionHandler()->ConnectToNetwork(
      network->path(), base::Bind(&StartConnectSuccessCallback, callback),
      base::Bind(&StartConnectFailureCallback, callback), false);
}

void StartDisconnectSuccessCallback(
    const arc::mojom::NetHost::StartDisconnectCallback& mojo_callback) {
  mojo_callback.Run(mojom::NetworkResult::SUCCESS);
}

void StartDisconnectFailureCallback(
    const arc::mojom::NetHost::StartDisconnectCallback& mojo_callback,
    const std::string& error_name,
    std::unique_ptr<base::DictionaryValue> error_data) {
  VLOG(1) << "StartDisconnectFailureCallback: " << error_name;
  mojo_callback.Run(mojom::NetworkResult::FAILURE);
}

void ArcNetHostImpl::StartDisconnect(const mojo::String& guid,
                                     const StartDisconnectCallback& callback) {
  const chromeos::NetworkState* network =
      GetStateHandler()->GetNetworkStateFromGuid(guid);

  if (!network) {
    callback.Run(mojom::NetworkResult::FAILURE);
    return;
  }

  GetNetworkConnectionHandler()->DisconnectNetwork(
      network->path(), base::Bind(&StartDisconnectSuccessCallback, callback),
      base::Bind(&StartDisconnectFailureCallback, callback));
}

void ArcNetHostImpl::GetWifiEnabledState(
    const GetWifiEnabledStateCallback& callback) {
  bool is_enabled = GetStateHandler()->IsTechnologyEnabled(
      chromeos::NetworkTypePattern::WiFi());
  callback.Run(is_enabled);
}

void ArcNetHostImpl::SetWifiEnabledState(
    bool is_enabled,
    const SetWifiEnabledStateCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  chromeos::NetworkStateHandler::TechnologyState state =
      GetStateHandler()->GetTechnologyState(
          chromeos::NetworkTypePattern::WiFi());
  // WiFi can't be enabled or disabled in these states.
  if ((state == chromeos::NetworkStateHandler::TECHNOLOGY_PROHIBITED) ||
      (state == chromeos::NetworkStateHandler::TECHNOLOGY_UNINITIALIZED) ||
      (state == chromeos::NetworkStateHandler::TECHNOLOGY_UNAVAILABLE)) {
    VLOG(1) << "SetWifiEnabledState failed due to WiFi state: " << state;
    callback.Run(false);
  } else {
    GetStateHandler()->SetTechnologyEnabled(
        chromeos::NetworkTypePattern::WiFi(), is_enabled,
        chromeos::network_handler::ErrorCallback());
    callback.Run(true);
  }
}

void ArcNetHostImpl::StartScan() {
  GetStateHandler()->RequestScan();
}

void ArcNetHostImpl::ScanCompleted(const chromeos::DeviceState* /*unused*/) {
  if (!arc_bridge_service()->net_instance()) {
    VLOG(2) << "NetInstance not ready yet";
    return;
  }
  if (arc_bridge_service()->net_version() < 1) {
    VLOG(1) << "NetInstance does not support ScanCompleted.";
    return;
  }

  arc_bridge_service()->net_instance()->ScanCompleted();
}

void ArcNetHostImpl::OnShuttingDown() {
  GetStateHandler()->RemoveObserver(this, FROM_HERE);
}

}  // namespace arc
