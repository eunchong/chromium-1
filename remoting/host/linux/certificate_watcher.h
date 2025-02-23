// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_LINUX_CERTIFICATE_WATCHER_H_
#define REMOTING_HOST_LINUX_CERTIFICATE_WATCHER_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "remoting/host/host_status_monitor.h"
#include "remoting/host/host_status_observer.h"

namespace remoting {

// This class watches the cert database and notifies the host to restart when
// a change of the database is detected. The runner script will restart the host
// when the host is killed then the new host will capture any new changes of the
// database.
//
// Acceptable false positives will be caused by desktop sessions and other
// external programs.
//
// Implements HostStatusObserver to defer restart action when the host is
// connected to a client.
class CertificateWatcher : public remoting::HostStatusObserver {
 public:
  CertificateWatcher(
      const base::Closure& restart_action,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);

  // The message loop of io_task_runner MUST be running after the destructor is
  // called, otherwise there will be memory leaks.
  ~CertificateWatcher() override;

  // Starts watching file changes
  // calling |restart_action_| when the host need to restart.
  void Start();

  // Sets the monitor to observe connection/disconnection events to toggle
  // the inhibit mode. Should be called after the watcher starts.
  // Adds |this| as an observer to the monitor.
  // Removes |this| as an observer from the old monitor if it is not null.
  void SetMonitor(base::WeakPtr<HostStatusMonitor> monitor);

  // HostStatusObserver interface.
  void OnClientConnected(const std::string& jid) override;
  void OnClientDisconnected(const std::string& jid) override;

  // Will only work before the watcher starts.
  void SetDelayForTests(const base::TimeDelta& delay);
  void SetWatchPathForTests(const base::FilePath& watch_path);

 private:
  // Returns true if the watcher has started.
  bool is_started() const;

  // Callback passed to |file_watcher_|. Runs in IO thread.
  static void OnCertDirectoryChanged(
      scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
      base::WeakPtr<CertificateWatcher> watcher_, const base::FilePath& path,
      bool error);

  // Runs in the caller's thread.
  void DirectoryChanged(const base::FilePath& path, bool error);

  // Called by |restart_timer_| when it's time to reset the host.
  // It will defer restart if |inhibit_restart_scheduled_| flag is set to true.
  void OnTimer();

  // Reference to the monitor
  base::WeakPtr<HostStatusMonitor> monitor_;

  // Called when a restart is scheduled.
  base::Closure restart_action_;

  // The runner that runs everything other than the file watcher.
  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner_;

  // The runner that runs the file watcher.
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  bool inhibit_mode_ = false;

  bool restart_pending_ = false;

  // Path of the certificate files/directories.
  base::FilePath cert_watch_path_;

  // The file watcher to watch changes inside the certificate folder.
  std::unique_ptr<base::FilePathWatcher> file_watcher_;

  // The time to wait to restart when it is scheduled.
  base::TimeDelta delay_;

  // Timer to delay the restart action.
  std::unique_ptr<base::DelayTimer> restart_timer_;

  base::WeakPtrFactory<CertificateWatcher> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(CertificateWatcher);
};

}  // namespace remoting

#endif  // REMOTING_HOST_LINUX_CERTIFICATE_WATCHER_H_
