/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/csr_deps.h"
#include "shared/source/helpers/aux_translation.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/non_copyable_or_moveable.h"
#include "shared/source/utilities/tag_allocator.h"

#include "pipe_control_args.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace NEO {
class CommandStreamReceiver;
class LinearStream;

namespace TimestampPacketSizeControl {
constexpr uint32_t preferredPacketCount = 16u;
}

#pragma pack(1)
template <typename TSize>
struct TimestampPackets {
    struct Packet {
        TSize contextStart = 1u;
        TSize globalStart = 1u;
        TSize contextEnd = 1u;
        TSize globalEnd = 1u;
    };

    static GraphicsAllocation::AllocationType getAllocationType() {
        return GraphicsAllocation::AllocationType::TIMESTAMP_PACKET_TAG_BUFFER;
    }

    bool isCompleted() const {
        if (DebugManager.flags.DisableAtomicForPostSyncs.get()) {
            return false;
        }

        for (uint32_t i = 0; i < packetsUsed; i++) {
            if ((packets[i].contextEnd == 1) || (packets[i].globalEnd == 1)) {
                return false;
            }
        }

        return true;
    }

    void initialize() {
        for (auto &packet : packets) {
            packet.contextStart = 1u;
            packet.globalStart = 1u;
            packet.contextEnd = 1u;
            packet.globalEnd = 1u;
        }
        packetsUsed = 1;
        implicitGpuDependenciesCount = 0;
    }

    uint32_t getImplicitGpuDependenciesCount() const { return implicitGpuDependenciesCount; }

    Packet packets[TimestampPacketSizeControl::preferredPacketCount];
    uint32_t implicitGpuDependenciesCount = 0;
    uint32_t packetsUsed = 1;
};
#pragma pack()

using TimestampPacketStorage = TimestampPackets<uint32_t>;

static_assert(((4 * TimestampPacketSizeControl::preferredPacketCount + 2) * sizeof(uint32_t)) == sizeof(TimestampPacketStorage),
              "This structure is consumed by GPU and has to follow specific restrictions for padding and size");

class TimestampPacketContainer : public NonCopyableClass {
  public:
    using Node = TagNode<TimestampPacketStorage>;

    TimestampPacketContainer() = default;
    TimestampPacketContainer(TimestampPacketContainer &&) = default;
    TimestampPacketContainer &operator=(TimestampPacketContainer &&) = default;
    MOCKABLE_VIRTUAL ~TimestampPacketContainer();

    const std::vector<Node *> &peekNodes() const { return timestampPacketNodes; }
    void add(Node *timestampPacketNode);
    void swapNodes(TimestampPacketContainer &timestampPacketContainer);
    void assignAndIncrementNodesRefCounts(const TimestampPacketContainer &inputTimestampPacketContainer);
    void resolveDependencies(bool clearAllDependencies);
    void makeResident(CommandStreamReceiver &commandStreamReceiver);

  protected:
    std::vector<Node *> timestampPacketNodes;
};

struct TimestampPacketDependencies : public NonCopyableClass {
    TimestampPacketContainer cacheFlushNodes;
    TimestampPacketContainer previousEnqueueNodes;
    TimestampPacketContainer barrierNodes;
    TimestampPacketContainer auxToNonAuxNodes;
    TimestampPacketContainer nonAuxToAuxNodes;
};

struct TimestampPacketHelper {
    static uint64_t getContextEndGpuAddress(const TagNode<TimestampPacketStorage> &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    }

    static uint64_t getGpuDependenciesCountGpuAddress(const TagNode<TimestampPacketStorage> &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + offsetof(TimestampPacketStorage, implicitGpuDependenciesCount);
    }

    static void overrideSupportedDevicesCount(uint32_t &numSupportedDevices);

    template <typename GfxFamily>
    static void programSemaphoreWithImplicitDependency(LinearStream &cmdStream, TagNode<TimestampPacketStorage> &timestampPacketNode, uint32_t numSupportedDevices) {
        using MI_ATOMIC = typename GfxFamily::MI_ATOMIC;
        using COMPARE_OPERATION = typename GfxFamily::MI_SEMAPHORE_WAIT::COMPARE_OPERATION;
        using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;

        auto compareAddress = getContextEndGpuAddress(timestampPacketNode);
        auto dependenciesCountAddress = getGpuDependenciesCountGpuAddress(timestampPacketNode);

        for (uint32_t packetId = 0; packetId < timestampPacketNode.tagForCpuAccess->packetsUsed; packetId++) {
            uint64_t compareOffset = packetId * sizeof(TimestampPacketStorage::Packet);
            EncodeSempahore<GfxFamily>::addMiSemaphoreWaitCommand(cmdStream, compareAddress + compareOffset, 1, COMPARE_OPERATION::COMPARE_OPERATION_SAD_NOT_EQUAL_SDD);
        }

        bool trackPostSyncDependencies = true;
        if (DebugManager.flags.DisableAtomicForPostSyncs.get()) {
            trackPostSyncDependencies = false;
        }

        if (trackPostSyncDependencies) {
            overrideSupportedDevicesCount(numSupportedDevices);

            for (uint32_t i = 0; i < numSupportedDevices; i++) {
                timestampPacketNode.incImplicitCpuDependenciesCount();
            }
            EncodeAtomic<GfxFamily>::programMiAtomic(cmdStream, dependenciesCountAddress,
                                                     MI_ATOMIC::ATOMIC_OPCODES::ATOMIC_4B_INCREMENT,
                                                     MI_ATOMIC::DATA_SIZE::DATA_SIZE_DWORD,
                                                     0u, 0u);
        }
    }

    template <typename GfxFamily>
    static void programCsrDependencies(LinearStream &cmdStream, const CsrDependencies &csrDependencies, uint32_t numSupportedDevices) {
        for (auto timestampPacketContainer : csrDependencies) {
            for (auto &node : timestampPacketContainer->peekNodes()) {
                TimestampPacketHelper::programSemaphoreWithImplicitDependency<GfxFamily>(cmdStream, *node, numSupportedDevices);
            }
        }
    }

    template <typename GfxFamily, AuxTranslationDirection auxTranslationDirection>
    static void programSemaphoreWithImplicitDependencyForAuxTranslation(LinearStream &cmdStream,
                                                                        const TimestampPacketDependencies *timestampPacketDependencies,
                                                                        const HardwareInfo &hwInfo, uint32_t numSupportedDevices) {
        auto &container = (auxTranslationDirection == AuxTranslationDirection::AuxToNonAux)
                              ? timestampPacketDependencies->auxToNonAuxNodes
                              : timestampPacketDependencies->nonAuxToAuxNodes;

        // cache flush after NDR, before NonAuxToAux
        if (auxTranslationDirection == AuxTranslationDirection::NonAuxToAux && timestampPacketDependencies->cacheFlushNodes.peekNodes().size() > 0) {
            UNRECOVERABLE_IF(timestampPacketDependencies->cacheFlushNodes.peekNodes().size() != 1);
            auto cacheFlushTimestampPacketGpuAddress = getContextEndGpuAddress(*timestampPacketDependencies->cacheFlushNodes.peekNodes()[0]);

            PipeControlArgs args(true);
            MemorySynchronizationCommands<GfxFamily>::addPipeControlAndProgramPostSyncOperation(
                cmdStream, GfxFamily::PIPE_CONTROL::POST_SYNC_OPERATION::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA,
                cacheFlushTimestampPacketGpuAddress, 0, hwInfo, args);
        }

        for (auto &node : container.peekNodes()) {
            TimestampPacketHelper::programSemaphoreWithImplicitDependency<GfxFamily>(cmdStream, *node, numSupportedDevices);
        }
    }

    template <typename GfxFamily, AuxTranslationDirection auxTranslationDirection>
    static size_t getRequiredCmdStreamSizeForAuxTranslationNodeDependency(size_t count, const HardwareInfo &hwInfo, bool cacheFlushForBcsRequired) {
        size_t size = count * TimestampPacketHelper::getRequiredCmdStreamSizeForNodeDependencyWithBlitEnqueue<GfxFamily>();

        if (auxTranslationDirection == AuxTranslationDirection::NonAuxToAux && cacheFlushForBcsRequired) {
            size += MemorySynchronizationCommands<GfxFamily>::getSizeForPipeControlWithPostSyncOperation(hwInfo);
        }

        return size;
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForNodeDependencyWithBlitEnqueue() {
        return sizeof(typename GfxFamily::MI_SEMAPHORE_WAIT) + sizeof(typename GfxFamily::MI_ATOMIC);
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForNodeDependency(TagNode<TimestampPacketStorage> &timestampPacketNode) {
        size_t totalMiSemaphoreWaitSize = timestampPacketNode.tagForCpuAccess->packetsUsed * sizeof(typename GfxFamily::MI_SEMAPHORE_WAIT);

        return totalMiSemaphoreWaitSize + sizeof(typename GfxFamily::MI_ATOMIC);
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSize(const CsrDependencies &csrDependencies) {
        size_t totalCommandsSize = 0;
        for (auto timestampPacketContainer : csrDependencies) {
            for (auto &node : timestampPacketContainer->peekNodes()) {
                totalCommandsSize += getRequiredCmdStreamSizeForNodeDependency<GfxFamily>(*node);
            }
        }

        return totalCommandsSize;
    }
};

} // namespace NEO
