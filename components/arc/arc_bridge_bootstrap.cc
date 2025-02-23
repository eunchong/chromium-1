// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/arc_bridge_bootstrap.h"

#include <fcntl.h>
#include <grp.h>
#include <unistd.h>

#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/posix/eintr_wrapper.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/threading/worker_pool.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "ipc/unix_domain_socket_util.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/platform_channel_utils_posix.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace arc {

namespace {

const base::FilePath::CharType kArcBridgeSocketPath[] =
    FILE_PATH_LITERAL("/var/run/chrome/arc_bridge.sock");

const char kArcBridgeSocketGroup[] = "arc-bridge";

class ArcBridgeBootstrapImpl : public ArcBridgeBootstrap {
 public:
  // The possible states of the bootstrap connection.  In the normal flow,
  // the state changes in the following sequence:
  //
  // STOPPED
  //   Start() ->
  // SOCKET_CREATING
  //   CreateSocket() -> OnSocketCreated() ->
  // STARTING
  //   StartArcInstance() -> OnInstanceStarted() ->
  // STARTED
  //   AcceptInstanceConnection() -> OnInstanceConnected() ->
  // READY
  //
  // When Stop() is called from any state, either because an operation
  // resulted in an error or because the user is logging out:
  //
  // (any)
  //   Stop() ->
  // STOPPING
  //   StopInstance() ->
  // STOPPED
  //
  // When the instance crashes while it was ready, it will be stopped:
  //   READY -> STOPPING -> STOPPED
  // and then restarted:
  //   STOPPED -> SOCKET_CREATING -> ... -> READY).
  enum class State {
    // ARC is not currently running.
    STOPPED,

    // An UNIX socket is being created.
    SOCKET_CREATING,

    // The request to start the instance has been sent.
    STARTING,

    // The instance has started. Waiting for it to connect to the IPC bridge.
    STARTED,

    // The instance is fully connected.
    READY,

    // The request to shut down the instance has been sent.
    STOPPING,
  };

  ArcBridgeBootstrapImpl();
  ~ArcBridgeBootstrapImpl() override;

  // ArcBridgeBootstrap:
  void Start() override;
  void Stop() override;

 private:
  // Creates the UNIX socket on the bootstrap thread and then processes its
  // file descriptor.
  static base::ScopedFD CreateSocket();
  void OnSocketCreated(base::ScopedFD fd);

  // Synchronously accepts a connection on |socket_fd| and then processes the
  // connected socket's file descriptor.
  static base::ScopedFD AcceptInstanceConnection(base::ScopedFD socket_fd);
  void OnInstanceConnected(base::ScopedFD fd);

  void SetState(State state);

  // DBus callbacks.
  void OnInstanceStarted(base::ScopedFD socket_fd, bool success);
  void OnInstanceStopped(bool success);

  // The state of the bootstrap connection.
  State state_ = State::STOPPED;

  base::ThreadChecker thread_checker_;

  // WeakPtrFactory to use callbacks.
  base::WeakPtrFactory<ArcBridgeBootstrapImpl> weak_factory_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ArcBridgeBootstrapImpl);
};

ArcBridgeBootstrapImpl::ArcBridgeBootstrapImpl() : weak_factory_(this) {}

ArcBridgeBootstrapImpl::~ArcBridgeBootstrapImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(state_ == State::STOPPED || state_ == State::STOPPING);
}

void ArcBridgeBootstrapImpl::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(delegate_);
  if (state_ != State::STOPPED) {
    VLOG(1) << "Start() called when instance is not stopped";
    return;
  }
  SetState(State::SOCKET_CREATING);
  base::PostTaskAndReplyWithResult(
      base::WorkerPool::GetTaskRunner(true).get(), FROM_HERE,
      base::Bind(&ArcBridgeBootstrapImpl::CreateSocket),
      base::Bind(&ArcBridgeBootstrapImpl::OnSocketCreated,
                 weak_factory_.GetWeakPtr()));
}

// static
base::ScopedFD ArcBridgeBootstrapImpl::CreateSocket() {
  base::FilePath socket_path(kArcBridgeSocketPath);

  int raw_fd = -1;
  if (!IPC::CreateServerUnixDomainSocket(socket_path, &raw_fd)) {
    return base::ScopedFD();
  }
  base::ScopedFD socket_fd(raw_fd);

  // Make socket blocking.
  int flags = HANDLE_EINTR(fcntl(socket_fd.get(), F_GETFL));
  if (flags == -1) {
    PLOG(ERROR) << "fcntl(F_GETFL)";
    return base::ScopedFD();
  }
  if (HANDLE_EINTR(fcntl(socket_fd.get(), F_SETFL, flags & ~O_NONBLOCK)) < 0) {
    PLOG(ERROR) << "fcntl(O_NONBLOCK)";
    return base::ScopedFD();
  }

  // Change permissions on the socket.
  struct group arc_bridge_group;
  struct group* arc_bridge_group_res = nullptr;
  char buf[10000];
  if (HANDLE_EINTR(getgrnam_r(kArcBridgeSocketGroup, &arc_bridge_group, buf,
                              sizeof(buf), &arc_bridge_group_res)) < 0) {
    PLOG(ERROR) << "getgrnam_r";
    return base::ScopedFD();
  }

  if (!arc_bridge_group_res) {
    LOG(ERROR) << "Group '" << kArcBridgeSocketGroup << "' not found";
    return base::ScopedFD();
  }

  if (HANDLE_EINTR(chown(kArcBridgeSocketPath, -1, arc_bridge_group.gr_gid)) <
      0) {
    PLOG(ERROR) << "chown";
    return base::ScopedFD();
  }

  if (!base::SetPosixFilePermissions(socket_path, 0660)) {
    PLOG(ERROR) << "Could not set permissions: " << socket_path.value();
    return base::ScopedFD();
  }

  return socket_fd;
}

void ArcBridgeBootstrapImpl::OnSocketCreated(base::ScopedFD socket_fd) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ != State::SOCKET_CREATING) {
    VLOG(1) << "Stop() called while connecting";
    return;
  }
  SetState(State::STARTING);

  if (!socket_fd.is_valid()) {
    LOG(ERROR) << "ARC: Error creating socket";
    Stop();
    return;
  }
  chromeos::SessionManagerClient* session_manager_client =
      chromeos::DBusThreadManager::Get()->GetSessionManagerClient();
  session_manager_client->StartArcInstance(
      kArcBridgeSocketPath,
      base::Bind(&ArcBridgeBootstrapImpl::OnInstanceStarted,
                 weak_factory_.GetWeakPtr(), base::Passed(&socket_fd)));
}

void ArcBridgeBootstrapImpl::OnInstanceStarted(base::ScopedFD socket_fd,
                                               bool success) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ != State::STARTING) {
    VLOG(1) << "Stop() called when ARC is not running";
    return;
  }
  if (!success) {
    LOG(ERROR) << "Failed to start ARC instance";
    // Roll back the state to SOCKET_CREATING to avoid sending the D-Bus signal
    // to stop the failed instance.
    SetState(State::SOCKET_CREATING);
    Stop();
    return;
  }
  SetState(State::STARTED);

  base::PostTaskAndReplyWithResult(
      base::WorkerPool::GetTaskRunner(true).get(), FROM_HERE,
      base::Bind(&ArcBridgeBootstrapImpl::AcceptInstanceConnection,
                 base::Passed(&socket_fd)),
      base::Bind(&ArcBridgeBootstrapImpl::OnInstanceConnected,
                 weak_factory_.GetWeakPtr()));
}

// static
base::ScopedFD ArcBridgeBootstrapImpl::AcceptInstanceConnection(
    base::ScopedFD socket_fd) {
  int raw_fd = -1;
  if (!IPC::ServerAcceptConnection(socket_fd.get(), &raw_fd)) {
    return base::ScopedFD();
  }
  base::ScopedFD scoped_fd(raw_fd);

  // Hardcode pid 0 since it is unused in mojo.
  const base::ProcessHandle kUnusedChildProcessHandle = 0;
  mojo::edk::ScopedPlatformHandle child_handle =
      mojo::edk::ChildProcessLaunched(kUnusedChildProcessHandle);

  mojo::edk::ScopedPlatformHandleVectorPtr handles(
      new mojo::edk::PlatformHandleVector{child_handle.release()});

  struct iovec iov = {const_cast<char*>(""), 1};
  ssize_t result = mojo::edk::PlatformChannelSendmsgWithHandles(
      mojo::edk::PlatformHandle(scoped_fd.get()), &iov, 1, handles->data(),
      handles->size());
  if (result == -1) {
    PLOG(ERROR) << "sendmsg";
    return base::ScopedFD();
  }

  return scoped_fd;
}

void ArcBridgeBootstrapImpl::OnInstanceConnected(base::ScopedFD fd) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ != State::STARTED) {
    VLOG(1) << "Stop() called when ARC is not running";
    return;
  }
  if (!fd.is_valid()) {
    LOG(ERROR) << "Invalid handle";
    return;
  }
  mojo::ScopedMessagePipeHandle server_pipe = mojo::edk::CreateMessagePipe(
      mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd.release())));
  if (!server_pipe.is_valid()) {
    LOG(ERROR) << "Invalid pipe";
    return;
  }
  SetState(State::READY);
  mojom::ArcBridgeInstancePtr instance;
  instance.Bind(mojo::InterfacePtrInfo<mojom::ArcBridgeInstance>(
      std::move(server_pipe), 0u));
  delegate_->OnConnectionEstablished(std::move(instance));
}

void ArcBridgeBootstrapImpl::Stop() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ == State::STOPPED || state_ == State::STOPPING) {
    VLOG(1) << "Stop() called when ARC is not running";
    return;
  }
  if (state_ == State::SOCKET_CREATING) {
    // This was stopped before the D-Bus command to start the instance. Skip
    // the D-Bus command to stop it.
    SetState(State::STOPPING);
    OnInstanceStopped(true);
    return;
  }
  SetState(State::STOPPING);
  chromeos::SessionManagerClient* session_manager_client =
      chromeos::DBusThreadManager::Get()->GetSessionManagerClient();
  session_manager_client->StopArcInstance(base::Bind(
      &ArcBridgeBootstrapImpl::OnInstanceStopped, weak_factory_.GetWeakPtr()));
}

void ArcBridgeBootstrapImpl::OnInstanceStopped(bool success) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // STOPPING is the only valid state for this function.
  // DCHECK on enum classes not supported.
  DCHECK(state_ == State::STOPPING);
  DCHECK(delegate_);
  SetState(State::STOPPED);
  delegate_->OnStopped();
}

void ArcBridgeBootstrapImpl::SetState(State state) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // DCHECK on enum classes not supported.
  DCHECK(state_ != state);
  state_ = state;
  VLOG(2) << "State: " << static_cast<uint32_t>(state_);
}

}  // namespace

// static
std::unique_ptr<ArcBridgeBootstrap> ArcBridgeBootstrap::Create() {
  return base::WrapUnique(new ArcBridgeBootstrapImpl());
}

}  // namespace arc
