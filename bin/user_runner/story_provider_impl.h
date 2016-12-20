// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/modular/services/user/story_data.fidl.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_point.h"

namespace modular {
class ApplicationContext;
class StoryControllerImpl;

namespace {
class DeleteStoryCall;
}  // namespace

class StoryProviderImpl : public StoryProvider, ledger::PageWatcher {
 public:
  StoryProviderImpl(
      ApplicationEnvironmentPtr environment,
      fidl::InterfaceHandle<ledger::Ledger> ledger,
      ledger::LedgerRepositoryPtr ledger_repository,
      fidl::InterfaceRequest<StoryProvider> story_provider_request);

  ~StoryProviderImpl() override = default;

  // Adds a non-lifecycle-governing binding to this |StoryProvider|.
  // The principal binding established in the constructor governs the
  // lifespan of this instance.
  void AddAuxiliaryBinding(fidl::InterfaceRequest<StoryProvider> request) {
    aux_bindings_.AddBinding(this, std::move(request));
  }

  // Called by empty binding set handler of StoryControllerImpl to remove
  // the corresponding entry.
  void PurgeController(const std::string& story_id);

  // Obtains the StoryData for an existing story from the ledger.
  void GetStoryData(const fidl::String& story_id,
                    const std::function<void(StoryDataPtr)>& result);

  // Used by CreateStory() to write story meta-data to the ledger.
  void WriteStoryData(StoryDataPtr story_data, std::function<void()> done);

  fidl::InterfaceHandle<ledger::LedgerRepository> DuplicateLedgerRepository();

  // Used by StoryControllerImpl.
  using Storage = StoryStorageImpl::Storage;
  std::shared_ptr<Storage> storage() { return storage_; }
  ledger::PagePtr GetStoryPage(const fidl::Array<uint8_t>& story_page_id);
  void ConnectToStoryRunnerFactory(
      fidl::InterfaceRequest<StoryRunnerFactory> request);
  void ConnectToResolver(fidl::InterfaceRequest<Resolver> request);

  using FidlDocMap = fidl::Map<fidl::String, document_store::DocumentPtr>;
  using FidlStringMap = fidl::Map<fidl::String, fidl::String>;

 private:
  // |StoryProvider|
  void CreateStory(const fidl::String& url,
                   const CreateStoryCallback& callback) override;

  // |StoryProvider|
  void CreateStoryWithInfo(
      const fidl::String& url,
      FidlStringMap extra_info,
      FidlDocMap root_docs,
      const CreateStoryWithInfoCallback& callback) override;

  // |StoryProvider|
  void DeleteStory(const fidl::String& story_id,
                   const DeleteStoryCallback& callback) override;

  // |StoryProvider|
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& story_info_callback) override;

  // |StoryProvider|
  void GetController(const fidl::String& story_id,
                     fidl::InterfaceRequest<StoryController> request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  // |StoryProvider|
  void Watch(fidl::InterfaceHandle<StoryProviderWatcher> watcher) override;

  // |PageWatcher|
  void OnInitialState(fidl::InterfaceHandle<ledger::PageSnapshot> page,
                      const OnInitialStateCallback& cb) override;

  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                const OnChangeCallback& cb) override;

  ApplicationEnvironmentPtr environment_;
  StrongBinding<StoryProvider> binding_;
  fidl::BindingSet<StoryProvider> aux_bindings_;
  ledger::LedgerPtr ledger_;

  // The apps that provide the services below which were started by
  // this service instance through launcher_. We retain their
  // controllers, such that when this service terminates (though it
  // current doesn't) it terminates those apps as well.
  fidl::InterfacePtrSet<ApplicationController> apps_;

  ApplicationLauncherPtr launcher_;
  ServiceProviderPtr story_runner_services_;
  ServiceProviderPtr resolver_services_;

  // A list of IDs of *all* stories available on a user's ledger.
  std::unordered_set<std::string> story_ids_;

  // This is a container of all operations that are currently enqueued to run in
  // a FIFO manner. All operations exposed via |StoryProvider| interface is
  // queued here.
  //
  // The advantage of doing this is that if an operations consists of multiple
  // asynchronous calls then no state needs to be maintained for incomplete /
  // pending operations.
  OperationQueue operation_queue_;

  // This is a container of all Operations that can be run concurrently.
  OperationCollection operation_collection_;

  std::shared_ptr<Storage> storage_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;

  fidl::InterfacePtrSet<StoryProviderWatcher> watchers_;

  std::unordered_map<std::string, std::unique_ptr<StoryControllerImpl>>
      story_controllers_;

  // This represents a delete operation that was created via |DeleteStory()|
  // but is awaiting the corresponding |PageWatcher::OnChange()| before the
  // operation can be marked as done.
  //
  // Note that delete operations taking place on remote devices can still
  // trigger a new delete operations.
  std::pair<std::string, DeleteStoryCall*> pending_deletion_;

  ledger::LedgerRepositoryPtr ledger_repository_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
