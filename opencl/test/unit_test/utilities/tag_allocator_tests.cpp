/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/timestamp_packet.h"
#include "shared/source/utilities/tag_allocator.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"
#include "shared/test/common/mocks/ult_device_factory.h"

#include "opencl/test/unit_test/fixtures/memory_allocator_fixture.h"
#include "test.h"

#include "gtest/gtest.h"

#include <cstdint>

using namespace NEO;

struct TagAllocatorTest : public Test<MemoryAllocatorFixture> {
    const DeviceBitfield deviceBitfield{0xf};
    DebugManagerStateRestore restorer;

    void SetUp() override {
        DebugManager.flags.CreateMultipleSubDevices.set(4);
        MemoryAllocatorFixture::SetUp();
    }
};

struct TimeStamps {
    void initialize() {
        start = 1;
        end = 2;
        release = true;
    }
    static GraphicsAllocation::AllocationType getAllocationType() {
        return GraphicsAllocation::AllocationType::PROFILING_TAG_BUFFER;
    }
    bool isCompleted() const { return release; }
    uint32_t getImplicitGpuDependenciesCount() const { return 0; }
    bool release;
    uint64_t start;
    uint64_t end;
};

template <typename TagType>
class MockTagAllocator : public TagAllocator<TagType> {
    using BaseClass = TagAllocator<TagType>;
    using TagNodeT = TagNode<TagType>;

  public:
    using BaseClass::deferredTags;
    using BaseClass::doNotReleaseNodes;
    using BaseClass::freeTags;
    using BaseClass::populateFreeTags;
    using BaseClass::releaseDeferredTags;
    using BaseClass::usedTags;

    MockTagAllocator(MemoryManager *memMngr, size_t tagCount, size_t tagAlignment, bool disableCompletionCheck, DeviceBitfield deviceBitfield)
        : BaseClass(0, memMngr, tagCount, tagAlignment, sizeof(TagType), disableCompletionCheck, deviceBitfield) {
    }

    MockTagAllocator(MemoryManager *memMngr, size_t tagCount, size_t tagAlignment, DeviceBitfield deviceBitfield)
        : MockTagAllocator(memMngr, tagCount, tagAlignment, false, deviceBitfield) {
    }

    GraphicsAllocation *getGraphicsAllocation(size_t id = 0) {
        return this->gfxAllocations[id];
    }

    TagNodeT *getFreeTagsHead() {
        return this->freeTags.peekHead();
    }

    TagNodeT *getUsedTagsHead() {
        return this->usedTags.peekHead();
    }

    size_t getGraphicsAllocationsCount() {
        return this->gfxAllocations.size();
    }

    size_t getTagPoolCount() {
        return this->tagPoolMemory.size();
    }
};

TEST_F(TagAllocatorTest, givenTagNodeTypeWhenCopyingOrMovingThenDisallow) {
    EXPECT_FALSE(std::is_move_constructible<TagNode<TimeStamps>>::value);
    EXPECT_FALSE(std::is_copy_constructible<TagNode<TimeStamps>>::value);
}

TEST_F(TagAllocatorTest, WhenTagAllocatorIsCreatedThenItIsCorrectlyInitialized) {

    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 100, 64, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getGraphicsAllocation());

    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());
    EXPECT_EQ(nullptr, tagAllocator.getUsedTagsHead());

    void *gfxMemory = tagAllocator.getGraphicsAllocation()->getUnderlyingBuffer();
    void *head = reinterpret_cast<void *>(tagAllocator.getFreeTagsHead()->tagForCpuAccess);
    EXPECT_EQ(gfxMemory, head);
}

TEST_F(TagAllocatorTest, WhenGettingAndReturningTagThenFreeAndUsedListsAreUpdated) {

    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 10, 16, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getGraphicsAllocation());
    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());
    EXPECT_EQ(nullptr, tagAllocator.getUsedTagsHead());

    TagNode<TimeStamps> *tagNode = tagAllocator.getTag();

    EXPECT_NE(nullptr, tagNode);

    IDList<TagNode<TimeStamps>> &freeList = tagAllocator.freeTags;
    IDList<TagNode<TimeStamps>> &usedList = tagAllocator.usedTags;

    bool isFoundOnUsedList = usedList.peekContains(*tagNode);
    bool isFoundOnFreeList = freeList.peekContains(*tagNode);

    EXPECT_FALSE(isFoundOnFreeList);
    EXPECT_TRUE(isFoundOnUsedList);

    tagAllocator.returnTag(tagNode);

    isFoundOnUsedList = usedList.peekContains(*tagNode);
    isFoundOnFreeList = freeList.peekContains(*tagNode);

    EXPECT_TRUE(isFoundOnFreeList);
    EXPECT_FALSE(isFoundOnUsedList);
}

TEST_F(TagAllocatorTest, WhenTagAllocatorIsCreatedThenItPopulatesTagsWithProperDeviceBitfield) {
    size_t alignment = 64;

    EXPECT_NE(deviceBitfield, memoryManager->recentlyPassedDeviceBitfield);
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 10, alignment, deviceBitfield);
    EXPECT_EQ(deviceBitfield, memoryManager->recentlyPassedDeviceBitfield);
}

TEST_F(TagAllocatorTest, WhenTagIsAllocatedThenItIsAligned) {
    size_t alignment = 64;
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 10, alignment, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());

    TagNode<TimeStamps> *tagNode = tagAllocator.getTag();

    ASSERT_NE(nullptr, tagNode);
    EXPECT_EQ(0u, (uintptr_t)tagNode->tagForCpuAccess % alignment);

    tagAllocator.returnTag(tagNode);
}

TEST_F(TagAllocatorTest, givenTagAllocatorWhenAllNodesWereUsedThenCreateNewGraphicsAllocation) {

    // Big alignment to force only 4 tags
    size_t alignment = 1024;
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 4, alignment, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());

    TagNode<TimeStamps> *tagNodes[4];

    for (size_t i = 0; i < 4; i++) {
        tagNodes[i] = tagAllocator.getTag();
        EXPECT_NE(nullptr, tagNodes[i]);
    }
    EXPECT_EQ(1u, tagAllocator.getGraphicsAllocationsCount());
    EXPECT_EQ(1u, tagAllocator.getTagPoolCount());

    TagNode<TimeStamps> *tagNode = tagAllocator.getTag();
    EXPECT_NE(nullptr, tagNode);

    EXPECT_EQ(2u, tagAllocator.getGraphicsAllocationsCount());
    EXPECT_EQ(2u, tagAllocator.getTagPoolCount());
}

TEST_F(TagAllocatorTest, givenInputTagCountWhenCreatingAllocatorThenRequestedNumberOfNodesIsCreated) {
    class MyMockMemoryManager : public MockMemoryManager {
      public:
        using MockMemoryManager::MockMemoryManager;
        GraphicsAllocation *allocateGraphicsMemoryWithAlignment(const AllocationData &allocationData) override {
            return new MemoryAllocation(0, TimestampPacketStorage::getAllocationType(), nullptr, nullptr, 0, MemoryConstants::pageSize,
                                        1, MemoryPool::System4KBPages, false, false, mockMaxOsContextCount);
        }
    };

    auto mockMemoryManager = std::make_unique<MyMockMemoryManager>(true, true, *executionEnvironment);

    const size_t tagsCount = 3;
    MockTagAllocator<TimestampPacketStorage> tagAllocator(mockMemoryManager.get(), tagsCount, 1, deviceBitfield);

    size_t nodesFound = 0;
    auto head = tagAllocator.freeTags.peekHead();

    while (head) {
        nodesFound++;
        head = head->next;
    }
    EXPECT_EQ(tagsCount, nodesFound);
}

TEST_F(TagAllocatorTest, GivenSpecificOrderWhenReturningTagsThenFreeListIsUpdatedCorrectly) {

    // Big alignment to force only 4 tags
    size_t alignment = 1024;
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 4, alignment, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());

    TagNode<TimeStamps> *tagNodes[4];

    for (int i = 0; i < 4; i++) {
        tagNodes[i] = tagAllocator.getTag();
        EXPECT_NE(nullptr, tagNodes[i]);
    }
    EXPECT_EQ(1u, tagAllocator.getGraphicsAllocationsCount());
    EXPECT_EQ(1u, tagAllocator.getTagPoolCount());

    TagNode<TimeStamps> *tagNode2 = tagAllocator.getTag();
    EXPECT_NE(nullptr, tagNode2);
    EXPECT_EQ(2u, tagAllocator.getGraphicsAllocationsCount());
    EXPECT_EQ(2u, tagAllocator.getTagPoolCount());

    IDList<TagNode<TimeStamps>> &freeList = tagAllocator.freeTags;
    bool isFoundOnFreeList = freeList.peekContains(*tagNodes[0]);
    EXPECT_FALSE(isFoundOnFreeList);

    tagAllocator.returnTag(tagNodes[2]);
    isFoundOnFreeList = freeList.peekContains(*tagNodes[2]);
    EXPECT_TRUE(isFoundOnFreeList);
    EXPECT_NE(nullptr, tagAllocator.getFreeTagsHead());

    tagAllocator.returnTag(tagNodes[3]);
    isFoundOnFreeList = freeList.peekContains(*tagNodes[3]);
    EXPECT_TRUE(isFoundOnFreeList);

    tagAllocator.returnTag(tagNodes[1]);
    isFoundOnFreeList = freeList.peekContains(*tagNodes[1]);
    EXPECT_TRUE(isFoundOnFreeList);

    isFoundOnFreeList = freeList.peekContains(*tagNodes[0]);
    EXPECT_FALSE(isFoundOnFreeList);

    tagAllocator.returnTag(tagNodes[0]);
}

TEST_F(TagAllocatorTest, WhenGettingTagsFromTwoPoolsThenTagsAreDifferent) {

    // Big alignment to force only 1 tag
    size_t alignment = 4096;
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, alignment, deviceBitfield);

    ASSERT_NE(nullptr, tagAllocator.getFreeTagsHead());

    TagNode<TimeStamps> *tagNode1, *tagNode2;

    tagNode1 = tagAllocator.getTag();
    ASSERT_NE(nullptr, tagNode1);

    tagNode2 = tagAllocator.getTag();
    ASSERT_NE(nullptr, tagNode2);

    EXPECT_EQ(2u, tagAllocator.getGraphicsAllocationsCount());
    EXPECT_EQ(2u, tagAllocator.getTagPoolCount());
    EXPECT_NE(tagNode1->getBaseGraphicsAllocation(), tagNode2->getBaseGraphicsAllocation());

    tagAllocator.returnTag(tagNode1);
    tagAllocator.returnTag(tagNode2);
}

TEST_F(TagAllocatorTest, WhenCleaningUpResourcesThenAllResourcesAreReleased) {

    // Big alignment to force only 1 tag
    size_t alignment = 4096;
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, alignment, deviceBitfield);

    TagNode<TimeStamps> *tagNode1, *tagNode2;

    // Allocate first Pool
    tagNode1 = tagAllocator.getTag();
    EXPECT_NE(nullptr, tagNode1);

    // Allocate second Pool
    tagNode2 = tagAllocator.getTag();
    ASSERT_NE(nullptr, tagNode2);

    // Two pools should have different gfxAllocations
    EXPECT_NE(tagNode1->getBaseGraphicsAllocation(), tagNode2->getBaseGraphicsAllocation());

    // Return tags
    tagAllocator.returnTag(tagNode1);
    tagAllocator.returnTag(tagNode2);

    // Should cleanup all resources
    tagAllocator.cleanUpResources();

    EXPECT_EQ(0u, tagAllocator.getGraphicsAllocationsCount());
}

TEST_F(TagAllocatorTest, whenNewTagIsTakenThenItIsInitialized) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 2, deviceBitfield);
    tagAllocator.getFreeTagsHead()->tagForCpuAccess->start = 3;
    tagAllocator.getFreeTagsHead()->tagForCpuAccess->end = 4;
    tagAllocator.getFreeTagsHead()->setProfilingCapable(false);

    auto node = tagAllocator.getTag();
    EXPECT_EQ(1u, node->tagForCpuAccess->start);
    EXPECT_EQ(2u, node->tagForCpuAccess->end);
    EXPECT_TRUE(node->isProfilingCapable());
}

TEST_F(TagAllocatorTest, givenMultipleReferencesOnTagWhenReleasingThenReturnWhenAllRefCountsAreReleased) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 2, 1, deviceBitfield);

    auto tag = tagAllocator.getTag();
    EXPECT_NE(nullptr, tagAllocator.getUsedTagsHead());
    tagAllocator.returnTag(tag);
    EXPECT_EQ(nullptr, tagAllocator.getUsedTagsHead()); // only 1 reference

    tag = tagAllocator.getTag();
    tag->incRefCount();
    EXPECT_NE(nullptr, tagAllocator.getUsedTagsHead());

    tagAllocator.returnTag(tag);
    EXPECT_NE(nullptr, tagAllocator.getUsedTagsHead()); // 1 reference left
    tagAllocator.returnTag(tag);
    EXPECT_EQ(nullptr, tagAllocator.getUsedTagsHead());
}

TEST_F(TagAllocatorTest, givenNotReadyTagWhenReturnedThenMoveToDeferredList) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 1, deviceBitfield);
    auto node = tagAllocator.getTag();

    node->tagForCpuAccess->release = false;
    EXPECT_TRUE(tagAllocator.deferredTags.peekIsEmpty());
    tagAllocator.returnTag(node);
    EXPECT_FALSE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_TRUE(tagAllocator.freeTags.peekIsEmpty());
}

TEST_F(TagAllocatorTest, givenTagNodeWhenCompletionCheckIsDisabledThenStatusIsMarkedAsNotReady) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 1, deviceBitfield);
    EXPECT_FALSE(tagAllocator.doNotReleaseNodes);
    auto node = tagAllocator.getTag();

    EXPECT_TRUE(node->canBeReleased());

    node->setDoNotReleaseNodes(true);
    EXPECT_FALSE(node->canBeReleased());

    tagAllocator.returnTag(node);
    EXPECT_FALSE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_TRUE(tagAllocator.freeTags.peekIsEmpty());
}

TEST_F(TagAllocatorTest, givenTagAllocatorWhenDisabledCompletionCheckThenNodeInheritsItsState) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 1, true, deviceBitfield);
    EXPECT_TRUE(tagAllocator.doNotReleaseNodes);

    auto node = tagAllocator.getTag();

    EXPECT_FALSE(node->canBeReleased());

    node->setDoNotReleaseNodes(false);
    EXPECT_TRUE(node->canBeReleased());

    tagAllocator.returnTag(node);
    EXPECT_TRUE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_FALSE(tagAllocator.freeTags.peekIsEmpty());
}

TEST_F(TagAllocatorTest, givenReadyTagWhenReturnedThenMoveToFreeList) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 1, deviceBitfield);
    auto node = tagAllocator.getTag();

    node->tagForCpuAccess->release = true;
    EXPECT_TRUE(tagAllocator.deferredTags.peekIsEmpty());
    tagAllocator.returnTag(node);
    EXPECT_TRUE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_FALSE(tagAllocator.freeTags.peekIsEmpty());
}

TEST_F(TagAllocatorTest, givenEmptyFreeListWhenAskingForNewTagThenTryToReleaseDeferredListFirst) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 1, 1, deviceBitfield);
    auto node = tagAllocator.getTag();

    node->tagForCpuAccess->release = false;
    tagAllocator.returnTag(node);
    node->tagForCpuAccess->release = false;
    EXPECT_TRUE(tagAllocator.freeTags.peekIsEmpty());
    node = tagAllocator.getTag();
    EXPECT_NE(nullptr, node);
    EXPECT_TRUE(tagAllocator.freeTags.peekIsEmpty()); // empty again - new pool wasnt allocated
}

TEST_F(TagAllocatorTest, givenTagsOnDeferredListWhenReleasingItThenMoveReadyTagsToFreePool) {
    MockTagAllocator<TimeStamps> tagAllocator(memoryManager, 2, 1, deviceBitfield); // pool with 2 tags
    auto node1 = tagAllocator.getTag();
    auto node2 = tagAllocator.getTag();

    node1->tagForCpuAccess->release = false;
    node2->tagForCpuAccess->release = false;
    tagAllocator.returnTag(node1);
    tagAllocator.returnTag(node2);

    tagAllocator.releaseDeferredTags();
    EXPECT_FALSE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_TRUE(tagAllocator.freeTags.peekIsEmpty());

    node1->tagForCpuAccess->release = true;
    tagAllocator.releaseDeferredTags();
    EXPECT_FALSE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_FALSE(tagAllocator.freeTags.peekIsEmpty());

    node2->tagForCpuAccess->release = true;
    tagAllocator.releaseDeferredTags();
    EXPECT_TRUE(tagAllocator.deferredTags.peekIsEmpty());
    EXPECT_FALSE(tagAllocator.freeTags.peekIsEmpty());
}

TEST_F(TagAllocatorTest, givenTagAllocatorWhenGraphicsAllocationIsCreatedThenSetValidllocationType) {
    TagAllocator<TimestampPacketStorage> timestampPacketAllocator(mockRootDeviceIndex, memoryManager, 1, 1, sizeof(TimestampPacketStorage), false, mockDeviceBitfield);
    TagAllocator<HwTimeStamps> hwTimeStampsAllocator(mockRootDeviceIndex, memoryManager, 1, 1, sizeof(HwTimeStamps), false, mockDeviceBitfield);
    TagAllocator<HwPerfCounter> hwPerfCounterAllocator(mockRootDeviceIndex, memoryManager, 1, 1, sizeof(HwPerfCounter), false, mockDeviceBitfield);

    auto timestampPacketTag = timestampPacketAllocator.getTag();
    auto hwTimeStampsTag = hwTimeStampsAllocator.getTag();
    auto hwPerfCounterTag = hwPerfCounterAllocator.getTag();

    EXPECT_EQ(GraphicsAllocation::AllocationType::TIMESTAMP_PACKET_TAG_BUFFER, timestampPacketTag->getBaseGraphicsAllocation()->getAllocationType());
    EXPECT_EQ(GraphicsAllocation::AllocationType::PROFILING_TAG_BUFFER, hwTimeStampsTag->getBaseGraphicsAllocation()->getAllocationType());
    EXPECT_EQ(GraphicsAllocation::AllocationType::PROFILING_TAG_BUFFER, hwPerfCounterTag->getBaseGraphicsAllocation()->getAllocationType());
}
