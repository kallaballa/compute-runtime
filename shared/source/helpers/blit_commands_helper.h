/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/csr_deps.h"
#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/helpers/aux_translation.h"
#include "shared/source/helpers/common_types.h"
#include "shared/source/helpers/constants.h"
#include "shared/source/helpers/vec.h"
#include "shared/source/utilities/stackvec.h"

#include <cstdint>
#include <functional>

namespace NEO {
class CommandStreamReceiver;
class Device;
class GraphicsAllocation;
class LinearStream;

struct RootDeviceEnvironment;

template <typename TagType>
struct TagNode;

template <typename TSize>
struct TimestampPackets;

struct BlitProperties;
struct HardwareInfo;
struct TimestampPacketDependencies;
using BlitPropertiesContainer = StackVec<BlitProperties, 16>;
using TimestampPacketStorage = TimestampPackets<uint32_t>;

struct BlitProperties {
    static BlitProperties constructPropertiesForReadWriteBuffer(BlitterConstants::BlitDirection blitDirection,
                                                                CommandStreamReceiver &commandStreamReceiver,
                                                                GraphicsAllocation *memObjAllocation,
                                                                GraphicsAllocation *preallocatedHostAllocation,
                                                                const void *hostPtr, uint64_t memObjGpuVa,
                                                                uint64_t hostAllocGpuVa, Vec3<size_t> hostPtrOffset,
                                                                Vec3<size_t> copyOffset, Vec3<size_t> copySize,
                                                                size_t hostRowPitch, size_t hostSlicePitch,
                                                                size_t gpuRowPitch, size_t gpuSlicePitch);

    static BlitProperties constructPropertiesForCopyBuffer(GraphicsAllocation *dstAllocation, GraphicsAllocation *srcAllocation,
                                                           Vec3<size_t> dstOffset, Vec3<size_t> srcOffset, Vec3<size_t> copySize,
                                                           size_t srcRowPitch, size_t srcSlicePitch,
                                                           size_t dstRowPitch, size_t dstSlicePitch, GraphicsAllocation *clearColorAllocation);

    static BlitProperties constructPropertiesForAuxTranslation(AuxTranslationDirection auxTranslationDirection,
                                                               GraphicsAllocation *allocation, GraphicsAllocation *clearColorAllocation);

    static void setupDependenciesForAuxTranslation(BlitPropertiesContainer &blitPropertiesContainer, TimestampPacketDependencies &timestampPacketDependencies,
                                                   TimestampPacketContainer &kernelTimestamps, const CsrDependencies &depsFromEvents,
                                                   CommandStreamReceiver &gpguCsr, CommandStreamReceiver &bcsCsr);

    TagNode<TimestampPacketStorage> *outputTimestampPacket = nullptr;
    BlitterConstants::BlitDirection blitDirection;
    CsrDependencies csrDependencies;
    AuxTranslationDirection auxTranslationDirection = AuxTranslationDirection::None;

    GraphicsAllocation *dstAllocation = nullptr;
    GraphicsAllocation *srcAllocation = nullptr;
    GraphicsAllocation *clearColorAllocation = nullptr;
    uint64_t dstGpuAddress = 0;
    uint64_t srcGpuAddress = 0;

    Vec3<size_t> copySize = 0;
    Vec3<size_t> dstOffset = 0;
    Vec3<size_t> srcOffset = 0;

    size_t dstRowPitch = 0;
    size_t dstSlicePitch = 0;
    size_t srcRowPitch = 0;
    size_t srcSlicePitch = 0;
    size_t bytesPerPixel = 0;
    Vec3<uint32_t> dstSize = 0;
    Vec3<uint32_t> srcSize = 0;
};

enum class BlitOperationResult {
    Unsupported,
    Fail,
    Success
};

namespace BlitHelperFunctions {
using BlitMemoryToAllocationFunc = std::function<BlitOperationResult(const Device &device,
                                                                     GraphicsAllocation *memory,
                                                                     size_t offset,
                                                                     const void *hostPtr,
                                                                     Vec3<size_t> size)>;
extern BlitMemoryToAllocationFunc blitMemoryToAllocation;
extern BlitMemoryToAllocationFunc blitAllocationToMemory;
} // namespace BlitHelperFunctions

struct BlitHelper {
    static BlitOperationResult blitMemoryToAllocation(const Device &device, GraphicsAllocation *memory, size_t offset, const void *hostPtr,
                                                      Vec3<size_t> size);
    static BlitOperationResult blitMemoryToAllocationBanks(const Device &device, GraphicsAllocation *memory, size_t offset, const void *hostPtr,
                                                           Vec3<size_t> size, DeviceBitfield memoryBanks);
    static BlitOperationResult blitAllocationToMemory(const Device &device, GraphicsAllocation *memory, size_t offset, const void *hostPtr,
                                                      Vec3<size_t> size);
};

template <typename GfxFamily>
struct BlitCommandsHelper {
    using COLOR_DEPTH = typename GfxFamily::XY_COLOR_BLT::COLOR_DEPTH;
    static uint64_t getMaxBlitWidth(const RootDeviceEnvironment &rootDeviceEnvironment);
    static uint64_t getMaxBlitWidthOverride(const RootDeviceEnvironment &rootDeviceEnvironment);
    static uint64_t getMaxBlitHeight(const RootDeviceEnvironment &rootDeviceEnvironment);
    static uint64_t getMaxBlitHeightOverride(const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchPreBlitCommand(LinearStream &linearStream);
    static size_t estimatePreBlitCommandSize();
    static void dispatchPostBlitCommand(LinearStream &linearStream);
    static size_t estimatePostBlitCommandSize();
    static size_t estimateBlitCommandsSize(const Vec3<size_t> &copySize, const CsrDependencies &csrDependencies, bool updateTimestampPacket,
                                           bool profilingEnabled, const RootDeviceEnvironment &rootDeviceEnvironment);
    static size_t estimateBlitCommandsSize(const BlitPropertiesContainer &blitPropertiesContainer, bool profilingEnabled,
                                           bool debugPauseEnabled, bool blitterDirectSubmission, const RootDeviceEnvironment &rootDeviceEnvironment);
    static size_t getNumberOfBlitsForCopyRegion(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment);
    static size_t getNumberOfBlitsForCopyPerRow(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment);
    static uint64_t calculateBlitCommandDestinationBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice);
    static uint64_t calculateBlitCommandSourceBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice);
    static uint64_t calculateBlitCommandDestinationBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice);
    static uint64_t calculateBlitCommandSourceBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice);
    static void dispatchBlitCommands(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchBlitCommandsForBufferRegion(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchBlitCommandsForBufferPerRow(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchBlitCommandsRegion(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchBlitMemoryColorFill(NEO::GraphicsAllocation *dstAlloc, uint32_t *pattern, size_t patternSize, LinearStream &linearStream, size_t size, const RootDeviceEnvironment &rootDeviceEnvironment);
    template <size_t patternSize>
    static void dispatchBlitMemoryFill(NEO::GraphicsAllocation *dstAlloc, uint32_t *pattern, LinearStream &linearStream, size_t size, const RootDeviceEnvironment &rootDeviceEnvironment, COLOR_DEPTH depth);
    static void appendBlitCommandsForBuffer(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void appendBlitCommandsForImages(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment, uint32_t &srcSlicePitch, uint32_t &dstSlicePitch);
    static void appendExtraMemoryProperties(typename GfxFamily::XY_COPY_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void appendExtraMemoryProperties(typename GfxFamily::XY_COLOR_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void appendColorDepth(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd);
    static void appendBlitCommandsForFillBuffer(NEO::GraphicsAllocation *dstAlloc, typename GfxFamily::XY_COLOR_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void appendSurfaceType(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd);
    static void appendTilingEnable(typename GfxFamily::XY_COLOR_BLT &blitCmd);
    static void appendTilingType(const GMM_TILE_TYPE srcTilingType, const GMM_TILE_TYPE dstTilingType, typename GfxFamily::XY_COPY_BLT &blitCmd);
    static void appendSliceOffsets(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd, uint32_t sliceIndex, const RootDeviceEnvironment &rootDeviceEnvironment, uint32_t srcSlicePitch, uint32_t dstSlicePitch);
    static void getBlitAllocationProperties(const GraphicsAllocation &allocation, uint32_t &pitch, uint32_t &qPitch, GMM_TILE_TYPE &tileType, uint32_t &mipTailLod, uint32_t &compressionDetails, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void dispatchDebugPauseCommands(LinearStream &commandStream, uint64_t debugPauseStateGPUAddress, DebugPauseState confirmationTrigger, DebugPauseState waitCondition);
    static size_t getSizeForDebugPauseCommands();
    static bool useOneBlitCopyCommand(Vec3<size_t> copySize, uint32_t bytesPerPixel);
    static uint32_t getAvailableBytesPerPixel(size_t copySize, uint32_t srcOrigin, uint32_t dstOrigin, uint32_t srcSize, uint32_t dstSize);
    static bool isCopyRegionPreferred(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment);
    static void programGlobalSequencerFlush(LinearStream &commandStream);
    static size_t getSizeForGlobalSequencerFlush();
    static bool miArbCheckWaRequired();
    static bool preBlitCommandWARequired();
    static void appendClearColor(const BlitProperties &blitProperties, typename GfxFamily::XY_COPY_BLT &blitCmd);
};
} // namespace NEO
