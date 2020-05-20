// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer.h"

#include <lib/fzl/vmar-manager.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

constexpr uint32_t kAudioRendererUnittestFrameRate = 48000;
constexpr size_t kAudioRendererUnittestVmoSize = 16ull * 1024;

class AudioRendererTest : public testing::ThreadingModelFixture {
 public:
  AudioRendererTest() {
    FX_CHECK(vmo_mapper_.CreateAndMap(kAudioRendererUnittestVmoSize,
                                      /*flags=*/0, nullptr, &vmo_) == ZX_OK);
  }

 protected:
  void SetUp() override {
    testing::ThreadingModelFixture::SetUp();

    renderer_ = AudioRenderer::Create(fidl_renderer_.NewRequest(), &context());
    EXPECT_NE(renderer_.get(), nullptr);
  }

  fuchsia::media::AudioStreamType PcmStreamType() {
    return fuchsia::media::AudioStreamType{
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .channels = 1,
        .frames_per_second = kAudioRendererUnittestFrameRate,
    };
  }

  // Creates a new payload buffer of |size| bytes and registers it with the renderer with |id|.
  //
  // A handle to the new VMO is returned.
  zx::vmo AddPayloadBuffer(uint32_t id, size_t size, fuchsia::media::AudioRenderer* renderer) {
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, zx::vmo::create(size, 0, &vmo));

    zx::vmo duplicate;
    EXPECT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate));
    renderer->AddPayloadBuffer(id, std::move(duplicate));
    RunLoopUntilIdle();
    return vmo;
  }

  void TearDown() override {
    // Dropping the channel queues up a reference to the Renderer through its error handler, which
    // will not work since the rest of this class is destructed before the loop and its
    // queued functions are. Here, we ensure the error handler runs before this class' destructors
    // run.
    { auto r = std::move(fidl_renderer_); }
    RunLoopUntilIdle();

    testing::ThreadingModelFixture::TearDown();
  }

 protected:
  fuchsia::media::AudioRendererPtr fidl_renderer_;
  std::shared_ptr<AudioRenderer> renderer_;

  fzl::VmoMapper vmo_mapper_;
  zx::vmo vmo_;

  fuchsia::media::AudioStreamType stream_type_ = {
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = kAudioRendererUnittestFrameRate,
  };
};

constexpr zx::duration kMinLeadTime = zx::nsec(123456789);
constexpr int64_t kInvalidLeadTimeNs = -1;

// Validate that MinLeadTime is provided to AudioRenderer clients accurately
TEST_F(AudioRendererTest, MinLeadTimePadding) {
  auto fake_output = testing::FakeAudioOutput::Create(
      &threading_model(), &context().device_manager(), &context().link_matrix());

  // We must set our output's lead time, before linking it, before calling SetPcmStreamType().
  fake_output->SetMinLeadTime(kMinLeadTime);

  // Our RouteGraph links one FakeAudioOutput to the Renderer-under-test. Thus we can set our
  // output's MinLeadTime, fully expecting this value to be reflected as-is to renderer+clients.
  context().route_graph().AddRenderer(std::move(renderer_));
  context().route_graph().AddDevice(fake_output.get());

  // SetPcmStreamType triggers the routing preparation completion, which connects output(s) to
  // renderer. Renderers react to new outputs in `OnLinkAdded` by recalculating minimum lead time.
  fidl_renderer_->SetPcmStreamType(PcmStreamType());
  RunLoopUntilIdle();

  auto lead_time_ns = kInvalidLeadTimeNs;
  fidl_renderer_->GetMinLeadTime(
      [&lead_time_ns](int64_t received_lead_time_ns) { lead_time_ns = received_lead_time_ns; });

  RunLoopUntilIdle();
  ASSERT_NE(lead_time_ns, kInvalidLeadTimeNs) << "No response received for GetMinLeadTime";
  EXPECT_EQ(lead_time_ns, kMinLeadTime.to_nsecs()) << "Incorrect GetMinLeadTime received";
}

TEST_F(AudioRendererTest, AllocatePacketQueueForLinks) {
  auto fake_output = testing::FakeAudioOutput::Create(
      &threading_model(), &context().device_manager(), &context().link_matrix());

  context().route_graph().AddRenderer(std::move(renderer_));
  context().route_graph().AddDevice(fake_output.get());

  fidl_renderer_->SetPcmStreamType(PcmStreamType());
  AddPayloadBuffer(0, PAGE_SIZE, fidl_renderer_.get());
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_offset = 128;
  fidl_renderer_->SendPacketNoReply(std::move(packet));
  RunLoopUntilIdle();

  std::vector<LinkMatrix::LinkHandle> links;
  context().link_matrix().SourceLinks(*fake_output, &links);
  ASSERT_EQ(1u, links.size());
  for (auto& link : links) {
    auto stream = link.stream;
    ASSERT_TRUE(stream);

    {  // Expect a buffer.
      auto buffer = stream->ReadLock(zx::time(0), 0, 0);
      ASSERT_TRUE(buffer);
      EXPECT_FALSE(buffer->is_continuous());
      EXPECT_NE(nullptr, buffer->payload());
    }
    {  // No more buffers.
      auto buffer = stream->ReadLock(zx::time(0), 0, 0);
      ASSERT_FALSE(buffer);
    }
  }
}

TEST_F(AudioRendererTest, RegistersWithRouteGraphIfHasUsageStreamTypeAndBuffers) {
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_), 0u);

  zx::vmo duplicate;
  ASSERT_EQ(
      vmo_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_READ | ZX_RIGHT_MAP, &duplicate),
      ZX_OK);

  auto output = testing::FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                                 &context().link_matrix());
  context().route_graph().AddDevice(output.get());
  RunLoopUntilIdle();

  auto* renderer_raw = renderer_.get();
  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(duplicate));

  RunLoopUntilIdle();
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);
}

TEST_F(AudioRendererTest, ReportsPlayAndPauseToPolicy) {
  auto output = testing::FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                                 &context().link_matrix());
  context().route_graph().AddDevice(output.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));

  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  RunLoopUntilIdle();
  EXPECT_TRUE(context().audio_admin().IsActive(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT));

  fidl_renderer_->PauseNoReply();
  RunLoopUntilIdle();
  EXPECT_FALSE(context().audio_admin().IsActive(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT));
}

TEST_F(AudioRendererTest, RemoveRendererWhileBufferLocked) {
  auto output = testing::FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                                 &context().link_matrix());
  context().route_graph().AddDevice(output.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Enqueue a packet
  fuchsia::media::StreamPacket packet;
  packet.pts = fuchsia::media::NO_TIMESTAMP;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = 128;
  fidl_renderer_->SendPacketNoReply(std::move(packet));
  RunLoopUntilIdle();

  // This will be the packet queue created when the link between the renderer and output was formed.
  auto packet_queue = output->stream();
  ASSERT_TRUE(packet_queue);

  // Acquire a buffer.
  auto buf = packet_queue->ReadLock(zx::time(0), 0, 32);
  ASSERT_TRUE(buf);
  EXPECT_EQ(0u, buf->start().Floor());
  EXPECT_EQ(32u, buf->length().Floor());

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopUntilIdle();

  // Now release the buffer.
  buf = std::nullopt;
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace media::audio
