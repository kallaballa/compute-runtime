/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/gfx_partition.h"

#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/heap_assigner.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {

const std::array<HeapIndex, 4> GfxPartition::heap32Names{{HeapIndex::HEAP_INTERNAL_DEVICE_MEMORY,
                                                          HeapIndex::HEAP_INTERNAL,
                                                          HeapIndex::HEAP_EXTERNAL_DEVICE_MEMORY,
                                                          HeapIndex::HEAP_EXTERNAL}};

const std::array<HeapIndex, 7> GfxPartition::heapNonSvmNames{{HeapIndex::HEAP_INTERNAL_DEVICE_MEMORY,
                                                              HeapIndex::HEAP_INTERNAL,
                                                              HeapIndex::HEAP_EXTERNAL_DEVICE_MEMORY,
                                                              HeapIndex::HEAP_EXTERNAL,
                                                              HeapIndex::HEAP_STANDARD,
                                                              HeapIndex::HEAP_STANDARD64KB,
                                                              HeapIndex::HEAP_EXTENDED}};

GfxPartition::GfxPartition(OSMemory::ReservedCpuAddressRange &sharedReservedCpuAddressRange) : reservedCpuAddressRange(sharedReservedCpuAddressRange), osMemory(OSMemory::create()) {}

GfxPartition::~GfxPartition() {
    osMemory->releaseCpuAddressRange(reservedCpuAddressRange);
    reservedCpuAddressRange = {0};
}

void GfxPartition::Heap::init(uint64_t base, uint64_t size) {
    this->base = base;
    this->size = size;

    // Exclude very first and very last 64K from GPU address range allocation
    if (size > 2 * GfxPartition::heapGranularity) {
        size -= 2 * GfxPartition::heapGranularity;
    }

    alloc = std::make_unique<HeapAllocator>(base + GfxPartition::heapGranularity, size);
}

void GfxPartition::Heap::initExternalWithFrontWindow(uint64_t base, uint64_t size) {
    this->base = base;
    this->size = size;

    size -= GfxPartition::heapGranularity;

    alloc = std::make_unique<HeapAllocator>(base, size, 0u);
}

void GfxPartition::freeGpuAddressRange(uint64_t ptr, size_t size) {
    for (auto heapName : GfxPartition::heapNonSvmNames) {
        auto &heap = getHeap(heapName);
        if ((ptr > heap.getBase()) && ((ptr + size) < heap.getLimit())) {
            heap.free(ptr, size);
            break;
        }
    }
}

bool GfxPartition::init(uint64_t gpuAddressSpace, size_t cpuAddressRangeSizeToReserve, uint32_t rootDeviceIndex, size_t numRootDevices, bool useFrontWindowPool) {

    /*
     * I. 64-bit builds:
     *
     *   1) 48-bit Full Range SVM gfx layout:
     *
     *                   SVM                  H0   H1   H2   H3      STANDARD      STANDARD64K
     *   |__________________________________|____|____|____|____|________________|______________|
     *   |                                  |    |    |    |    |                |              |
     *   |                                gfxBase                                             gfxTop
     *  0x0                          0x0000800000000000                               0x0000FFFFFFFFFFFF
     *
     *
     *   2) 47-bit Full Range SVM gfx layout:
     *
     *                             gfxSize = 2^47 / 4 = 0x200000000000
     *                      ________________________________________________
     *                     /                                                \
     *           SVM      / H0   H1   H2   H3      STANDARD      STANDARD64K \      SVM
     *   |________________|____|____|____|____|________________|______________|_______________|
     *   |                |    |    |    |    |                |              |               |
     *   |              gfxBase                                             gfxTop            |
     *  0x0    reserveCpuAddressRange(gfxSize)                                      0x00007FFFFFFFFFFF
     *   \_____________________________________ SVM _________________________________________/
     *
     *
     *
     *   3) Limited Range gfx layout (no SVM):
     *
     *     H0   H1   H2   H3        STANDARD          STANDARD64K
     *   |____|____|____|____|____________________|__________________|
     *   |    |    |    |    |                    |                  |
     * gfxBase                                                    gfxTop
     *  0x0                                                    0xFFF...FFF < 47 bit
     *
     *
     * II. 32-bit builds:
     *
     *   1) 32-bit Full Range SVM gfx layout:
     *
     *      SVM    H0   H1   H2   H3      STANDARD      STANDARD64K
     *   |_______|____|____|____|____|________________|______________|
     *   |       |    |    |    |    |                |              |
     *   |     gfxBase                                             gfxTop
     *  0x0  0x100000000                                       gpuAddressSpace
     */

    uint64_t gfxTop = gpuAddressSpace + 1;
    uint64_t gfxBase = 0x0ull;
    const uint64_t gfxHeap32Size = 4 * MemoryConstants::gigaByte;

    if (is32bit) {
        gfxBase = maxNBitValue(32) + 1;
        heapInit(HeapIndex::HEAP_SVM, 0ull, gfxBase);
    } else {
        if (gpuAddressSpace == maxNBitValue(48)) {
            gfxBase = maxNBitValue(48 - 1) + 1;
            heapInit(HeapIndex::HEAP_SVM, 0ull, gfxBase);
        } else if (gpuAddressSpace == maxNBitValue(47)) {
            if (reservedCpuAddressRange.alignedPtr == nullptr) {
                if (cpuAddressRangeSizeToReserve == 0) {
                    return false;
                }
                reservedCpuAddressRange = osMemory->reserveCpuAddressRange(cpuAddressRangeSizeToReserve, GfxPartition::heapGranularity);
                if (reservedCpuAddressRange.originalPtr == nullptr) {
                    return false;
                }
                if (!isAligned<GfxPartition::heapGranularity>(reservedCpuAddressRange.alignedPtr)) {
                    return false;
                }
            }
            gfxBase = reinterpret_cast<uint64_t>(reservedCpuAddressRange.alignedPtr);
            gfxTop = gfxBase + cpuAddressRangeSizeToReserve;
            heapInit(HeapIndex::HEAP_SVM, 0ull, gpuAddressSpace + 1);
        } else if (gpuAddressSpace < maxNBitValue(47)) {
            gfxBase = 0ull;
            heapInit(HeapIndex::HEAP_SVM, 0ull, 0ull);
        } else {
            if (!initAdditionalRange(gpuAddressSpace, gfxBase, gfxTop, rootDeviceIndex, numRootDevices)) {
                return false;
            }
        }
    }

    for (auto heap : GfxPartition::heap32Names) {
        if (useFrontWindowPool && HeapAssigner::heapTypeWithFrontWindowPool(heap)) {
            heapInitExternalWithFrontWindow(heap, gfxBase, gfxHeap32Size);
            size_t externalFrontWindowSize = GfxPartition::frontWindowPoolSize;
            heapInitExternalWithFrontWindow(HeapAssigner::mapExternalWindowIndex(heap), heapAllocate(heap, externalFrontWindowSize),
                                            externalFrontWindowSize);
        } else {
            heapInit(heap, gfxBase, gfxHeap32Size);
        }
        gfxBase += gfxHeap32Size;
    }

    uint64_t gfxStandardSize = alignDown((gfxTop - gfxBase) >> 1, heapGranularity);

    heapInit(HeapIndex::HEAP_STANDARD, gfxBase, gfxStandardSize);
    gfxBase += gfxStandardSize;

    // Split HEAP_STANDARD64K among root devices
    auto gfxStandard64KBSize = alignDown(gfxStandardSize / numRootDevices, GfxPartition::heapGranularity);
    heapInit(HeapIndex::HEAP_STANDARD64KB, gfxBase + rootDeviceIndex * gfxStandard64KBSize, gfxStandard64KBSize);

    return true;
}

} // namespace NEO
