/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/source/os_interface/os_memory.h"

#include "opencl/test/unit_test/mocks/mock_gfx_partition.h"

#include "gtest/gtest.h"

using namespace NEO;

constexpr size_t reservedCpuAddressRangeSize = is64bit ? (6 * 4 * GB) : 0;
constexpr uint64_t sizeHeap32 = 4 * MemoryConstants::gigaByte;

void testGfxPartition(MockGfxPartition &gfxPartition, uint64_t gfxBase, uint64_t gfxTop, uint64_t svmTop) {
    if (svmTop) {
        // SVM should be initialized
        EXPECT_TRUE(gfxPartition.heapInitialized(HeapIndex::HEAP_SVM));
        EXPECT_EQ(gfxPartition.getHeapBase(HeapIndex::HEAP_SVM), 0ull);
        EXPECT_EQ(gfxPartition.getHeapSize(HeapIndex::HEAP_SVM), svmTop);
        EXPECT_EQ(gfxPartition.getHeapLimit(HeapIndex::HEAP_SVM), svmTop - 1);
    } else {
        // Limited range
        EXPECT_FALSE(gfxPartition.heapInitialized(HeapIndex::HEAP_SVM));
    }

    for (auto heap32 : GfxPartition::heap32Names) {
        EXPECT_TRUE(gfxPartition.heapInitialized(heap32));
        EXPECT_TRUE(isAligned<GfxPartition::heapGranularity>(gfxPartition.getHeapBase(heap32)));
        EXPECT_EQ(gfxPartition.getHeapBase(heap32), gfxBase);
        EXPECT_EQ(gfxPartition.getHeapSize(heap32), sizeHeap32);
        gfxBase += sizeHeap32;
    }

    uint64_t sizeStandard = (gfxTop - gfxBase) >> 1;

    EXPECT_TRUE(gfxPartition.heapInitialized(HeapIndex::HEAP_STANDARD));
    auto heapStandardBase = gfxPartition.getHeapBase(HeapIndex::HEAP_STANDARD);
    auto heapStandardSize = gfxPartition.getHeapSize(HeapIndex::HEAP_STANDARD);
    EXPECT_TRUE(isAligned<GfxPartition::heapGranularity>(heapStandardBase));
    EXPECT_EQ(heapStandardBase, gfxBase);
    EXPECT_EQ(heapStandardSize, sizeStandard);

    gfxBase += sizeStandard;
    EXPECT_TRUE(gfxPartition.heapInitialized(HeapIndex::HEAP_STANDARD64KB));
    auto heapStandard64KbBase = gfxPartition.getHeapBase(HeapIndex::HEAP_STANDARD64KB);
    auto heapStandard64KbSize = gfxPartition.getHeapSize(HeapIndex::HEAP_STANDARD64KB);
    EXPECT_TRUE(isAligned<GfxPartition::heapGranularity>(heapStandard64KbBase));

    EXPECT_EQ(heapStandard64KbBase, heapStandardBase + heapStandardSize);
    EXPECT_EQ(heapStandard64KbSize, heapStandardSize);
    EXPECT_EQ(heapStandard64KbBase + heapStandard64KbSize, gfxTop);
    EXPECT_EQ(gfxBase + sizeStandard, gfxTop);

    size_t sizeSmall = MemoryConstants::pageSize;
    size_t sizeBig = 4 * MemoryConstants::megaByte + MemoryConstants::pageSize;
    for (auto heap : MockGfxPartition::allHeapNames) {
        if (!gfxPartition.heapInitialized(heap)) {
            EXPECT_TRUE(heap == HeapIndex::HEAP_SVM || heap == HeapIndex::HEAP_EXTENDED);
            continue;
        }

        EXPECT_GT(gfxPartition.getHeapMinimalAddress(heap), gfxPartition.getHeapBase(heap));
        EXPECT_EQ(gfxPartition.getHeapMinimalAddress(heap), gfxPartition.getHeapBase(heap) + GfxPartition::heapGranularity);

        auto ptrBig = gfxPartition.heapAllocate(heap, sizeBig);
        EXPECT_NE(ptrBig, 0ull);
        EXPECT_LT(gfxPartition.getHeapBase(heap), ptrBig);
        EXPECT_EQ(ptrBig, gfxPartition.getHeapBase(heap) + GfxPartition::heapGranularity);
        gfxPartition.heapFree(heap, ptrBig, sizeBig);

        auto ptrSmall = gfxPartition.heapAllocate(heap, sizeSmall);
        EXPECT_NE(ptrSmall, 0ull);
        EXPECT_LT(gfxPartition.getHeapBase(heap), ptrSmall);
        EXPECT_GT(gfxPartition.getHeapLimit(heap), ptrSmall);
        EXPECT_EQ(ptrSmall, gfxPartition.getHeapBase(heap) + gfxPartition.getHeapSize(heap) - GfxPartition::heapGranularity - sizeSmall);
        gfxPartition.heapFree(heap, ptrSmall, sizeSmall);
    }
}

TEST(GfxPartitionTest, testGfxPartitionFullRange48BitSVM) {
    MockGfxPartition gfxPartition;
    gfxPartition.init(maxNBitValue(48), reservedCpuAddressRangeSize, 0, 1);

    uint64_t gfxTop = maxNBitValue(48) + 1;
    uint64_t gfxBase = MemoryConstants::maxSvmAddress + 1;

    testGfxPartition(gfxPartition, gfxBase, gfxTop, gfxBase);
}

TEST(GfxPartitionTest, testGfxPartitionFullRange47BitSVM) {
    MockGfxPartition gfxPartition;
    gfxPartition.init(maxNBitValue(47), reservedCpuAddressRangeSize, 0, 1);

    uint64_t gfxBase = is32bit ? MemoryConstants::maxSvmAddress + 1 : (uint64_t)gfxPartition.getReservedCpuAddressRange();
    uint64_t gfxTop = is32bit ? maxNBitValue(47) + 1 : gfxBase + gfxPartition.getReservedCpuAddressRangeSize();
    uint64_t svmTop = MemoryConstants::maxSvmAddress + 1;

    testGfxPartition(gfxPartition, gfxBase, gfxTop, svmTop);
}

TEST(GfxPartitionTest, testGfxPartitionLimitedRange) {
    MockGfxPartition gfxPartition;
    gfxPartition.init(maxNBitValue(47 - 1), reservedCpuAddressRangeSize, 0, 1);

    uint64_t gfxBase = is32bit ? MemoryConstants::maxSvmAddress + 1 : 0ull;
    uint64_t gfxTop = maxNBitValue(47 - 1) + 1;
    uint64_t svmTop = gfxBase;

    testGfxPartition(gfxPartition, gfxBase, gfxTop, svmTop);
}

TEST(GfxPartitionTest, testGfxPartitionUnsupportedRange) {
    if (is32bit) {
        GTEST_SKIP();
    }

    MockGfxPartition gfxPartition;
    EXPECT_FALSE(gfxPartition.init(maxNBitValue(48 + 1), reservedCpuAddressRangeSize, 0, 1));
}

TEST(GfxPartitionTest, testGfxPartitionFullRange48BitSVMHeap64KBSplit) {
    uint32_t rootDeviceIndex = 3;
    size_t numRootDevices = 5;

    MockGfxPartition gfxPartition;
    gfxPartition.init(maxNBitValue(48), reservedCpuAddressRangeSize, rootDeviceIndex, numRootDevices);

    uint64_t gfxBase = is32bit ? MemoryConstants::maxSvmAddress + 1 : maxNBitValue(48 - 1) + 1;
    uint64_t gfxTop = maxNBitValue(48) + 1;

    auto heapStandardSize = (gfxTop - gfxBase - 4 * sizeHeap32) / 2;
    auto heapStandard64KBSize = alignDown(heapStandardSize / numRootDevices, GfxPartition::heapGranularity);

    EXPECT_EQ(heapStandard64KBSize, gfxPartition.getHeapSize(HeapIndex::HEAP_STANDARD64KB));
    EXPECT_EQ(gfxBase + 4 * sizeHeap32 + heapStandardSize + rootDeviceIndex * heapStandard64KBSize, gfxPartition.getHeapBase(HeapIndex::HEAP_STANDARD64KB));
}

TEST(GfxPartitionTest, testGfxPartitionFullRange47BitSVMHeap64KBSplit) {
    uint32_t rootDeviceIndex = 3;
    size_t numRootDevices = 5;

    MockGfxPartition gfxPartition;
    gfxPartition.init(maxNBitValue(47), reservedCpuAddressRangeSize, rootDeviceIndex, numRootDevices);

    uint64_t gfxBase = is32bit ? MemoryConstants::maxSvmAddress + 1 : (uint64_t)gfxPartition.getReservedCpuAddressRange();
    uint64_t gfxTop = is32bit ? maxNBitValue(47) + 1 : gfxBase + gfxPartition.getReservedCpuAddressRangeSize();

    auto heapStandardSize = ((gfxTop - gfxBase) - 4 * sizeHeap32) / 2;
    auto heapStandard64KBSize = alignDown(heapStandardSize / numRootDevices, GfxPartition::heapGranularity);

    EXPECT_EQ(heapStandard64KBSize, gfxPartition.getHeapSize(HeapIndex::HEAP_STANDARD64KB));
    EXPECT_EQ(gfxBase + 4 * sizeHeap32 + heapStandardSize + rootDeviceIndex * heapStandard64KBSize, gfxPartition.getHeapBase(HeapIndex::HEAP_STANDARD64KB));
}

class MockOsMemory : public OSMemory {
  public:
    MockOsMemory() = default;
    ~MockOsMemory() { reserveCount = 0; }
    uint32_t getReserveCount() { return reserveCount; }
    struct MockOSMemoryReservedCpuAddressRange : public OSMemory::ReservedCpuAddressRange {
        MockOSMemoryReservedCpuAddressRange(void *ptr, size_t size, size_t align) {
            alignedPtr = originalPtr = ptr;
            sizeToReserve = actualReservedSize = size;
        }
    };

    OSMemory::ReservedCpuAddressRange reserveCpuAddressRange(size_t sizeToReserve, size_t alignment) override {
        return reserveCpuAddressRange(0, sizeToReserve, alignment);
    };

    OSMemory::ReservedCpuAddressRange reserveCpuAddressRange(void *baseAddress, size_t sizeToReserve, size_t alignment) override {
        reserveCount++;
        return MockOSMemoryReservedCpuAddressRange(returnAddress, sizeToReserve, alignment);
    };

    void getMemoryMaps(MemoryMaps &outMemoryMaps) override {}

    void releaseCpuAddressRange(const OSMemory::ReservedCpuAddressRange &reservedCpuAddressRange) override{};

    void *osReserveCpuAddressRange(void *baseAddress, size_t sizeToReserve) override { return nullptr; }
    void osReleaseCpuAddressRange(void *reservedCpuAddressRange, size_t reservedSize) override {}

    void *returnAddress = reinterpret_cast<void *>(0x10000);
    static uint32_t reserveCount;
};

uint32_t MockOsMemory::reserveCount = 0;

TEST(GfxPartitionTest, given47bitGpuAddressSpaceWhenInitializingMultipleGfxPartitionsThenReserveCpuAddressRangeForDriverAllocationsOnlyOnce) {
    if (is32bit) {
        GTEST_SKIP();
    }

    OSMemory::ReservedCpuAddressRange reservedCpuAddressRange;
    std::vector<std::unique_ptr<MockGfxPartition>> gfxPartitions;
    for (int i = 0; i < 10; ++i) {
        gfxPartitions.push_back(std::make_unique<MockGfxPartition>(reservedCpuAddressRange));
        gfxPartitions[i]->osMemory.reset(new MockOsMemory);
        gfxPartitions[i]->init(maxNBitValue(47), reservedCpuAddressRangeSize, i, 10);
    }

    EXPECT_EQ(1u, static_cast<MockOsMemory *>(gfxPartitions[0]->osMemory.get())->getReserveCount());
}

TEST(GfxPartitionTest, testGfxPartitionFullRange47BitSVMFailedIfReservedCpuRangeSizeIsZero) {
    if (is32bit) {
        GTEST_SKIP();
    }

    MockGfxPartition gfxPartition;
    EXPECT_FALSE(gfxPartition.init(maxNBitValue(47), 0, 0, 1));
}

TEST(GfxPartitionTest, testGfxPartitionFullRange47BitSVMFailedIfReturnedReservedCpuRangeIsNull) {
    if (is32bit) {
        GTEST_SKIP();
    }

    auto mockOsMemory = new MockOsMemory;
    mockOsMemory->returnAddress = nullptr;
    MockGfxPartition gfxPartition;
    gfxPartition.osMemory.reset(mockOsMemory);
    EXPECT_FALSE(gfxPartition.init(maxNBitValue(47), reservedCpuAddressRangeSize, 0, 1));
}

TEST(GfxPartitionTest, testGfxPartitionFullRange47BitSVMFailedIfReturnedReservedCpuRangeIsNotAligned) {
    if (is32bit) {
        GTEST_SKIP();
    }

    auto mockOsMemory = new MockOsMemory;
    mockOsMemory->returnAddress = reinterpret_cast<void *>(0x10001);
    MockGfxPartition gfxPartition;
    gfxPartition.osMemory.reset(mockOsMemory);
    EXPECT_FALSE(gfxPartition.init(maxNBitValue(47), reservedCpuAddressRangeSize, 0, 1));
}
