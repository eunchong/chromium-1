// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/fuzzed_socket_factory.h"

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "net/base/address_list.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_change_notifier.h"
#include "net/log/net_log.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/fuzzed_socket.h"
#include "net/socket/ssl_client_socket.h"
#include "net/ssl/ssl_failure_state.h"
#include "net/udp/datagram_client_socket.h"

namespace net {

namespace {

// Datagram ClientSocket implementation that always failed to connect.
class FailingUDPClientSocket : public DatagramClientSocket {
 public:
  FailingUDPClientSocket() {}
  ~FailingUDPClientSocket() override {}

  // DatagramClientSocket implementation:
  int Connect(const IPEndPoint& address) override { return ERR_FAILED; }

  int ConnectUsingNetwork(NetworkChangeNotifier::NetworkHandle network,
                          const IPEndPoint& address) override {
    return ERR_FAILED;
  }

  int ConnectUsingDefaultNetwork(const IPEndPoint& address) override {
    return ERR_FAILED;
  }

  NetworkChangeNotifier::NetworkHandle GetBoundNetwork() const override {
    return -1;
  }

  // DatagramSocket implementation:
  void Close() override {}

  int GetPeerAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  int GetLocalAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  const BoundNetLog& NetLog() const override { return net_log_; }

  // Socket implementation:
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int SetReceiveBufferSize(int32_t size) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int SetSendBufferSize(int32_t size) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(FailingUDPClientSocket);
};

// SSLClientSocket implementation that always fails to connect.
class FailingSSLClientSocket : public SSLClientSocket {
 public:
  FailingSSLClientSocket() {}
  ~FailingSSLClientSocket() override {}

  // Socket implementation:
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int SetReceiveBufferSize(int32_t size) override { return OK; }
  int SetSendBufferSize(int32_t size) override { return OK; }

  // StreamSocket implementation:
  int Connect(const CompletionCallback& callback) override {
    return ERR_FAILED;
  }

  void Disconnect() override {}
  bool IsConnected() const override { return false; }
  bool IsConnectedAndIdle() const override { return false; }

  int GetPeerAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  int GetLocalAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  const BoundNetLog& NetLog() const override { return net_log_; }

  void SetSubresourceSpeculation() override {}
  void SetOmniboxSpeculation() override {}

  bool WasEverUsed() const override { return false; }

  void EnableTCPFastOpenIfSupported() override {}

  bool WasNpnNegotiated() const override { return false; }

  NextProto GetNegotiatedProtocol() const override { return kProtoUnknown; }

  bool GetSSLInfo(SSLInfo* ssl_info) override { return false; }

  void GetConnectionAttempts(ConnectionAttempts* out) const override {
    out->clear();
  }

  void ClearConnectionAttempts() override {}

  void AddConnectionAttempts(const ConnectionAttempts& attempts) override {}

  int64_t GetTotalReceivedBytes() const override { return 0; }

  // SSLSocket implementation:
  int ExportKeyingMaterial(const base::StringPiece& label,
                           bool has_context,
                           const base::StringPiece& context,
                           unsigned char* out,
                           unsigned int outlen) override {
    NOTREACHED();
    return 0;
  }

  // SSLClientSocket implementation:
  void GetSSLCertRequestInfo(SSLCertRequestInfo* cert_request_info) override {}

  NextProtoStatus GetNextProto(std::string* proto) const override {
    return NextProtoStatus::kNextProtoUnsupported;
  }

  ChannelIDService* GetChannelIDService() const override {
    NOTREACHED();
    return nullptr;
  }

  Error GetSignedEKMForTokenBinding(crypto::ECPrivateKey* key,
                                    std::vector<uint8_t>* out) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  crypto::ECPrivateKey* GetChannelIDKey() const override {
    NOTREACHED();
    return nullptr;
  }

  SSLFailureState GetSSLFailureState() const override {
    return SSL_FAILURE_UNKNOWN;
  }

 private:
  BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(FailingSSLClientSocket);
};

}  // namespace

FuzzedSocketFactory::FuzzedSocketFactory(FuzzedDataProvider* data_provider)
    : data_provider_(data_provider) {}

FuzzedSocketFactory::~FuzzedSocketFactory() {}

std::unique_ptr<DatagramClientSocket>
FuzzedSocketFactory::CreateDatagramClientSocket(
    DatagramSocket::BindType bind_type,
    const RandIntCallback& rand_int_cb,
    NetLog* net_log,
    const NetLog::Source& source) {
  return base::WrapUnique(new FailingUDPClientSocket());
}

std::unique_ptr<StreamSocket> FuzzedSocketFactory::CreateTransportClientSocket(
    const AddressList& addresses,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetLog* net_log,
    const NetLog::Source& source) {
  std::unique_ptr<FuzzedSocket> socket(
      new FuzzedSocket(data_provider_, net_log));
  socket->set_fuzz_connect_result(true);
  // Just use the first address.
  socket->set_remote_address(*addresses.begin());
  return std::move(socket);
}

std::unique_ptr<SSLClientSocket> FuzzedSocketFactory::CreateSSLClientSocket(
    std::unique_ptr<ClientSocketHandle> transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    const SSLClientSocketContext& context) {
  return base::WrapUnique(new FailingSSLClientSocket());
}

void FuzzedSocketFactory::ClearSSLSessionCache() {}

}  // namespace net
