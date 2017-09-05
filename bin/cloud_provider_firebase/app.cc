// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/ledger/cloud_provider_firebase/factory_impl.h"
#include "apps/modular/services/lifecycle/lifecycle.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "garnet/public/lib/fidl/cpp/bindings/binding_set.h"
#include "garnet/public/lib/ftl/log_settings_command_line.h"
#include "garnet/public/lib/mtl/tasks/message_loop.h"

namespace cloud_provider_firebase {
namespace {

class App : public modular::Lifecycle {
 public:
  App()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
    FTL_DCHECK(application_context_);
    tracing::InitializeTracer(application_context_.get(),
                              {"cloud_provider_firebase"});
  }

  void Run() {
    application_context_->outgoing_services()->AddService<modular::Lifecycle>(
        [this](fidl::InterfaceRequest<modular::Lifecycle> request) {
          lifecycle_bindings_.AddBinding(this, std::move(request));
        });
    application_context_->outgoing_services()->AddService<Factory>(
        [this](fidl::InterfaceRequest<Factory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

  void Terminate() override { loop_.PostQuitTask(); }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  mtl::MessageLoop loop_;
  FactoryImpl factory_impl_;
  fidl::BindingSet<modular::Lifecycle> lifecycle_bindings_;
  fidl::BindingSet<Factory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};
}  // namespace

}  // namespace cloud_provider_firebase

int main(int argc, const char** argv) {
  const auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  ftl::SetLogSettingsFromCommandLine(command_line);

  cloud_provider_firebase::App app;
  app.Run();

  return 0;
}
