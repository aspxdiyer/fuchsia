// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_PCI_DEVICE_H
#define MSD_INTEL_PCI_DEVICE_H

#include "gtt.h"
#include "interrupt_manager.h"
#include "platform_pci_device.h"
#include "platform_semaphore.h"

class MsdIntelPciDevice : public magma::PlatformPciDevice {
public:
    std::unique_ptr<magma::PlatformInterrupt> RegisterInterrupt() override
    {
        DASSERT(false);
        return nullptr;
    }

    // Additional core device implmementation that may reside inside a separate core driver.
    virtual bool RegisterInterruptCallback(InterruptManager::InterruptCallback callback, void* data,
                                           uint32_t interrupt_mask) = 0;
    virtual void UnregisterInterruptCallback() = 0;
    virtual Gtt* GetGtt() = 0;

    // To be removed upon display driver separation.
    virtual void
    PresentBuffer(uint32_t buffer_handle, magma_system_image_descriptor* image_desc,
                  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                  present_buffer_callback_t callback) = 0;

    static std::unique_ptr<MsdIntelPciDevice> Create(void* platform_device_handle);
};

#endif // MSD_INTEL_PCI_DEVICE_H
