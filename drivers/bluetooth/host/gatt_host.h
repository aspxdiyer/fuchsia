// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include <fuchsia/cpp/bluetooth_gatt.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bthost {

class GattClientServer;
class GattServerServer;

// This object is responsible for bridging the GATT profile to the outside
// world.
//
// GattHost represents the GATT TaskDomain. It spawns and manages a thread on
// which all GATT tasks are serialized in an asynchronous event loop.
//
// This domain is responsible for:
//
//   * The GATT profile implementation over L2CAP;
//   * Creation of child GATT bt-gatt-svc devices and relaying of their
//     messages;
//   * FIDL message processing
//
// All functions on this object are thread safe. ShutDown() must be called
// at least once to properly clean up this object before destruction (this is
// asserted).
class GattHost final : public fbl::RefCounted<GattHost>,
                       public btlib::common::TaskDomain<GattHost> {
 public:
  static fbl::RefPtr<GattHost> Create(std::string thread_name);

  void Initialize();

  // This MUST be called to cleanly destroy this object. This method is
  // thread-safe.
  void ShutDown();

  // Closes all open FIDL interface handles.
  void CloseServers();

  // Binds the given GATT server request to a FIDL server.
  void BindGattServer(fidl::InterfaceRequest<bluetooth_gatt::Server> request);

  // Binds the given GATT client request to a FIDL server.
  void BindGattClient(std::string peer_id,
                      fidl::InterfaceRequest<bluetooth_gatt::Client> request);

  // Unbinds a previously bound GATT client server associated with |peer_id|.
  void UnbindGattClient(std::string peer_id);

  // Returns the GATT profile implementation.
  fbl::RefPtr<btlib::gatt::GATT> profile() const { return gatt_; }

  // Sets a remote service handler to be notified when remote GATT services are
  // discovered. These are used by HostDevice to publish bt-gatt-svc devices.
  // This method is thread-safe. |callback| will not be called after ShutDown().
  void SetRemoteServiceWatcher(
      btlib::gatt::GATT::RemoteServiceWatcher callback);

 private:
  BT_FRIEND_TASK_DOMAIN(GattHost);
  friend class fbl::RefPtr<GattHost>;

  explicit GattHost(std::string thread_name);
  ~GattHost() override;

  // Called by TaskDomain during shutdown. This must be called on the GATT task
  // runner.
  void CleanUp();

  // Closes the active FIDL servers. This must be called on the GATT thread.
  void CloseServersInternal();

  std::mutex mtx_;
  btlib::gatt::GATT::RemoteServiceWatcher remote_service_watcher_
      __TA_GUARDED(mtx_);

  // NOTE: All members below must be accessed on the GATT thread

  // The GATT profile.
  fbl::RefPtr<btlib::gatt::GATT> gatt_;

  // All currently active FIDL connections. These involve FIDL bindings that are
  // bound to |task_runner_|. These objects are highly thread hostile and must
  // be accessed only from |task_runner_|'s thread.
  std::unordered_map<GattServerServer*, std::unique_ptr<GattServerServer>>
      server_servers_;

  // Mapping from peer identifiers to GattClient pointers. These are tracked by
  // the peer IDs for easy removal on disconnection.
  std::unordered_map<std::string, std::unique_ptr<GattClientServer>>
      client_servers_;

  fxl::WeakPtrFactory<GattHost> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattHost);
};

}  // namespace bthost
