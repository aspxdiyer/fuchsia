// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_address_space.h"

bool MockAddressSpace::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
{
    DASSERT(magma::is_page_aligned(size));
    allocations_[next_addr_] = Allocation{size, true, true};
    *addr_out = next_addr_;
    next_addr_ += size;
    return true;
}

bool MockAddressSpace::Free(uint64_t addr)
{
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
        return false;
    iter->second.allocated = false;
    return true;
}

bool MockAddressSpace::Clear(uint64_t addr)
{
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
        return false;
    iter->second.clear = true;
    return true;
}

bool MockAddressSpace::Insert(uint64_t addr, magma::PlatformBuffer* buffer,
                              CachingType caching_type)
{
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
        return false;
    iter->second.clear = false;
    return true;
}
