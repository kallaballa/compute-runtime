/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/constants.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/memory_manager/memory_banks.h"

#include <atomic>
#include <mutex>

namespace NEO {

class PhysicalAddressAllocator {
  public:
    PhysicalAddressAllocator() {
        mainAllocator.store(initialPageAddress);
    }

    virtual ~PhysicalAddressAllocator() = default;

    uint64_t reserve4kPage(uint32_t memoryBank) {
        return reservePage(memoryBank, MemoryConstants::pageSize, MemoryConstants::pageSize);
    }

    uint64_t reserve64kPage(uint32_t memoryBank) {
        return reservePage(memoryBank, MemoryConstants::pageSize64k, MemoryConstants::pageSize64k);
    }

    virtual uint64_t reservePage(uint32_t memoryBank, size_t pageSize, size_t alignement) {
        UNRECOVERABLE_IF(memoryBank != MemoryBanks::MainBank);

        std::unique_lock<std::mutex> lock(pageReserveMutex);

        auto currentAddress = mainAllocator.load();
        auto alignmentSize = alignUp(currentAddress, alignement) - currentAddress;
        mainAllocator += alignmentSize;
        return mainAllocator.fetch_add(pageSize);
    }

  protected:
    std::atomic<uint64_t> mainAllocator;
    std::mutex pageReserveMutex;
    const uint64_t initialPageAddress = 0x1000;
};

} // namespace NEO
