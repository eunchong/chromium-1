// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/mojo/service_registry_impl.h"

#include <utility>

#include "mojo/common/common_type_converters.h"

namespace content {

ServiceRegistry* ServiceRegistry::Create() {
  return new ServiceRegistryImpl;
}

ServiceRegistryImpl::ServiceRegistryImpl()
    : binding_(this), weak_factory_(this) {}

ServiceRegistryImpl::~ServiceRegistryImpl() {
  while (!pending_connects_.empty()) {
    mojo::CloseRaw(pending_connects_.front().second);
    pending_connects_.pop();
  }
}

void ServiceRegistryImpl::Bind(shell::mojom::InterfaceProviderRequest request) {
  binding_.Bind(std::move(request));
  binding_.set_connection_error_handler(base::Bind(
      &ServiceRegistryImpl::OnConnectionError, base::Unretained(this)));
}

void ServiceRegistryImpl::BindRemoteServiceProvider(
    shell::mojom::InterfaceProviderPtr service_provider) {
  CHECK(!remote_provider_);
  remote_provider_ = std::move(service_provider);
  while (!pending_connects_.empty()) {
    remote_provider_->GetInterface(
        mojo::String::From(pending_connects_.front().first),
        mojo::ScopedMessagePipeHandle(pending_connects_.front().second));
    pending_connects_.pop();
  }
}

void ServiceRegistryImpl::AddService(const std::string& service_name,
                                     const ServiceFactory service_factory) {
  service_factories_[service_name] = service_factory;
}

void ServiceRegistryImpl::RemoveService(const std::string& service_name) {
  service_factories_.erase(service_name);
}

void ServiceRegistryImpl::ConnectToRemoteService(
    const base::StringPiece& service_name,
    mojo::ScopedMessagePipeHandle handle) {
  auto override_it = service_overrides_.find(service_name.as_string());
  if (override_it != service_overrides_.end()) {
    override_it->second.Run(std::move(handle));
    return;
  }

  if (!remote_provider_) {
    pending_connects_.push(
        std::make_pair(service_name.as_string(), handle.release()));
    return;
  }
  remote_provider_->GetInterface(
      mojo::String::From(service_name.as_string()), std::move(handle));
}

void ServiceRegistryImpl::AddServiceOverrideForTesting(
    const std::string& service_name,
    const ServiceFactory& factory) {
  service_overrides_[service_name] = factory;
}

void ServiceRegistryImpl::ClearServiceOverridesForTesting() {
  service_overrides_.clear();
}

bool ServiceRegistryImpl::IsBound() const {
  return binding_.is_bound();
}

base::WeakPtr<ServiceRegistry> ServiceRegistryImpl::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void ServiceRegistryImpl::GetInterface(
    const mojo::String& name,
    mojo::ScopedMessagePipeHandle client_handle) {
  auto it = service_factories_.find(name);
  if (it == service_factories_.end()) {
    DLOG(ERROR) << name << " not found";
    return;
  }

  // It's possible and effectively unavoidable that under certain conditions
  // an invalid handle may be received. Don't invoke the factory in that case.
  if (!client_handle.is_valid()) {
    DVLOG(2) << "Invalid pipe handle for " << name << " interface request.";
    return;
  }

  it->second.Run(std::move(client_handle));
}

void ServiceRegistryImpl::OnConnectionError() {
  binding_.Close();
}

}  // namespace content
