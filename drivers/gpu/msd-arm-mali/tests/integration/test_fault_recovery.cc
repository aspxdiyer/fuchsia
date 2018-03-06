// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <fcntl.h>
#include <poll.h>
#include <thread>

#include "helper/platform_device_helper.h"
#include "magma.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "gtest/gtest.h"

namespace {

class TestBase {
public:
    TestBase() { fd_ = open("/dev/class/gpu/000", O_RDONLY); }

    int fd() { return fd_; }

    ~TestBase() { close(fd_); }

private:
    int fd_;
};

enum JobDescriptorType { kJobDescriptorTypeNop = 1 };

struct JobDescriptorHeader {
    uint64_t reserved1;
    uint64_t reserved2;
    uint8_t job_descriptor_size : 1;
    uint8_t job_type : 7;
    uint8_t reserved3;
    uint16_t reserved4;
    uint16_t reserved5;
    uint16_t reserved6;
    uint64_t next_job;
};

class TestConnection : public TestBase {
public:
    TestConnection()
    {
        connection_ = magma_create_connection(fd(), MAGMA_CAPABILITY_RENDERING);
        DASSERT(connection_);

        magma_create_context(connection_, &context_id_);
    }

    ~TestConnection()
    {
        magma_release_context(connection_, context_id_);

        if (connection_)
            magma_release_connection(connection_);
    }

    enum How { NORMAL, JOB_FAULT, MMU_FAULT };

    void SubmitCommandBuffer(How how)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size;
        magma_buffer_t job_buffer;

        ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &job_buffer), 0);
        uint64_t job_va;
        InitJobBuffer(job_buffer, &job_va);

        std::vector<uint8_t> vaddr(sizeof(magma_arm_mali_atom));

        ASSERT_TRUE(InitBatchBuffer(vaddr.data(), vaddr.size(), job_va, how));

        magma_system_inline_command_buffer command_buffer;
        command_buffer.data = vaddr.data();
        command_buffer.size = vaddr.size();
        command_buffer.semaphores = nullptr;
        command_buffer.semaphore_count = 0;
        magma_execute_immediate_commands(connection_, context_id_, 1, &command_buffer);

        int notification_fd = magma_get_notification_channel_fd(connection_);

        pollfd poll_fd = {notification_fd, POLLIN, 0};
        ASSERT_EQ(1, poll(&poll_fd, 1, -1));

        magma_arm_mali_status status;
        uint64_t status_size;
        EXPECT_EQ(MAGMA_STATUS_OK, magma_read_notification_channel(connection_, &status,
                                                                   sizeof(status), &status_size));
        EXPECT_EQ(status_size, sizeof(status));
        EXPECT_EQ(1u, status.atom_number);

        switch (how) {
            case NORMAL:
                EXPECT_EQ(kArmMaliResultSuccess, status.result_code);
                break;

            case JOB_FAULT:
                EXPECT_EQ(kArmMaliResultConfigFault, status.result_code);
                break;

            case MMU_FAULT:
                EXPECT_EQ(kArmMaliResultReadFault, status.result_code);
                break;
        }

        magma_release_buffer(connection_, job_buffer);
    }

    bool InitBatchBuffer(void* vaddr, uint64_t size, uint64_t job_va, How how)
    {
        memset(vaddr, 0, size);

        magma_arm_mali_atom* atom = static_cast<magma_arm_mali_atom*>(vaddr);
        if (how == JOB_FAULT) {
            // An unaligned job chain address should fail with error 0x40.
            atom->job_chain_addr = 1;
        } else if (how == MMU_FAULT) {
            atom->job_chain_addr = job_va - PAGE_SIZE;
            if (atom->job_chain_addr == 0)
                atom->job_chain_addr = PAGE_SIZE * 2;
        } else {
            atom->job_chain_addr = job_va;
        }
        atom->atom_number = 1;

        return true;
    }

    bool InitJobBuffer(magma_buffer_t buffer, uint64_t* job_va)
    {
        void* vaddr;
        if (magma_map(connection_, buffer, &vaddr) != 0)
            return DRETF(false, "couldn't map job buffer");
        *job_va = (uint64_t)vaddr;
        magma_map_buffer_gpu(connection_, buffer, 0, 1, *job_va,
                             MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE |
                                 kMagmaArmMaliGpuMapFlagInnerShareable);
        magma_commit_buffer(connection_, buffer, 0, 1);
        JobDescriptorHeader* header = static_cast<JobDescriptorHeader*>(vaddr);
        memset(header, 0, sizeof(*header));
        header->job_descriptor_size = 1; // Next job address is 64-bit.
        header->job_type = kJobDescriptorTypeNop;
        header->next_job = 0;
        magma_clean_cache(buffer, 0, PAGE_SIZE, MAGMA_CACHE_OPERATION_CLEAN);
        return true;
    }

private:
    magma_connection_t* connection_;
    uint32_t context_id_;
};

} // namespace

TEST(FaultRecovery, Test)
{
    std::unique_ptr<TestConnection> test;
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::JOB_FAULT);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
}

TEST(FaultRecovery, TestMmu)
{
    std::unique_ptr<TestConnection> test;
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::MMU_FAULT);
    test.reset(new TestConnection());
    test->SubmitCommandBuffer(TestConnection::NORMAL);
}
