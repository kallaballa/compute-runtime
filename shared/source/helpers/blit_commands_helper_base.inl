/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/gmm_helper/resource_info.h"
#include "shared/source/helpers/blit_commands_helper.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/timestamp_packet.h"

namespace NEO {

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::getMaxBlitWidth(const RootDeviceEnvironment &rootDeviceEnvironment) {
    if (DebugManager.flags.LimitBlitterMaxWidth.get() != -1) {
        return static_cast<uint64_t>(DebugManager.flags.LimitBlitterMaxWidth.get());
    }
    auto maxBlitWidthOverride = getMaxBlitWidthOverride(rootDeviceEnvironment);
    if (maxBlitWidthOverride > 0) {
        return maxBlitWidthOverride;
    }
    return BlitterConstants::maxBlitWidth;
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::getMaxBlitHeight(const RootDeviceEnvironment &rootDeviceEnvironment) {
    if (DebugManager.flags.LimitBlitterMaxHeight.get() != -1) {
        return static_cast<uint64_t>(DebugManager.flags.LimitBlitterMaxHeight.get());
    }
    auto maxBlitHeightOverride = getMaxBlitHeightOverride(rootDeviceEnvironment);
    if (maxBlitHeightOverride > 0) {
        return maxBlitHeightOverride;
    }
    return BlitterConstants::maxBlitHeight;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchPreBlitCommand(LinearStream &linearStream) {
    if (BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired()) {
        MiFlushArgs args;
        EncodeMiFlushDW<GfxFamily>::programMiFlushDw(linearStream, 0, 0, args);
    }
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimatePreBlitCommandSize() {
    if (BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired()) {
        return EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite();
    }

    return 0u;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchPostBlitCommand(LinearStream &linearStream) {
    MiFlushArgs args;
    if (DebugManager.flags.PostBlitCommand.get() != BlitterConstants::PostBlitMode::Default) {
        switch (DebugManager.flags.PostBlitCommand.get()) {
        case BlitterConstants::PostBlitMode::MiArbCheck:
            EncodeMiArbCheck<GfxFamily>::program(linearStream);
            return;
        case BlitterConstants::PostBlitMode::MiFlush:
            EncodeMiFlushDW<GfxFamily>::programMiFlushDw(linearStream, 0, 0, args);
            return;
        default:
            return;
        }
    }

    if (BlitCommandsHelper<GfxFamily>::miArbCheckWaRequired()) {
        EncodeMiFlushDW<GfxFamily>::programMiFlushDw(linearStream, 0, 0, args);
    }

    EncodeMiArbCheck<GfxFamily>::program(linearStream);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimatePostBlitCommandSize() {
    if (DebugManager.flags.PostBlitCommand.get() != BlitterConstants::PostBlitMode::Default) {
        switch (DebugManager.flags.PostBlitCommand.get()) {
        case BlitterConstants::PostBlitMode::MiArbCheck:
            return EncodeMiArbCheck<GfxFamily>::getCommandSize();
        case BlitterConstants::PostBlitMode::MiFlush:
            return EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite();
        default:
            return 0;
        }
    }

    if (BlitCommandsHelper<GfxFamily>::miArbCheckWaRequired()) {
        return (EncodeMiArbCheck<GfxFamily>::getCommandSize() + EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite());
    }

    return EncodeMiArbCheck<GfxFamily>::getCommandSize();
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimateBlitCommandsSize(const Vec3<size_t> &copySize, const CsrDependencies &csrDependencies,
                                                               bool updateTimestampPacket, bool profilingEnabled,
                                                               const RootDeviceEnvironment &rootDeviceEnvironment) {
    size_t timestampCmdSize = 0;
    if (updateTimestampPacket) {
        timestampCmdSize += EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite();
        if (profilingEnabled) {
            timestampCmdSize += getProfilingMmioCmdsSize();
        }
    }

    bool preferRegionCopy = isCopyRegionPreferred(copySize, rootDeviceEnvironment);
    auto nBlits = preferRegionCopy ? getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment)
                                   : getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);

    auto sizePerBlit = (sizeof(typename GfxFamily::XY_COPY_BLT) + estimatePostBlitCommandSize());

    return TimestampPacketHelper::getRequiredCmdStreamSize<GfxFamily>(csrDependencies) +
           TimestampPacketHelper::getRequiredCmdStreamSizeForTaskCountContainer<GfxFamily>(csrDependencies) +
           (sizePerBlit * nBlits) +
           timestampCmdSize +
           estimatePreBlitCommandSize();
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimateBlitCommandsSize(const BlitPropertiesContainer &blitPropertiesContainer,
                                                               bool profilingEnabled, bool debugPauseEnabled,
                                                               bool blitterDirectSubmission, const RootDeviceEnvironment &rootDeviceEnvironment) {
    size_t size = 0;
    for (auto &blitProperties : blitPropertiesContainer) {
        size += BlitCommandsHelper<GfxFamily>::estimateBlitCommandsSize(blitProperties.copySize, blitProperties.csrDependencies,
                                                                        blitProperties.outputTimestampPacket != nullptr, profilingEnabled,
                                                                        rootDeviceEnvironment);
    }
    size += MemorySynchronizationCommands<GfxFamily>::getSizeForAdditonalSynchronization(*rootDeviceEnvironment.getHardwareInfo());
    size += EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite();
    if (blitterDirectSubmission) {
        size += sizeof(typename GfxFamily::MI_BATCH_BUFFER_START);
    } else {
        size += sizeof(typename GfxFamily::MI_BATCH_BUFFER_END);
    }

    if (debugPauseEnabled) {
        size += BlitCommandsHelper<GfxFamily>::getSizeForDebugPauseCommands();
    }

    size += BlitCommandsHelper<GfxFamily>::getSizeForGlobalSequencerFlush();

    return alignUp(size, MemoryConstants::cacheLineSize);
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandDestinationBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice) {
    return blitProperties.dstGpuAddress + blitProperties.dstOffset.x + offset +
           blitProperties.dstOffset.y * blitProperties.dstRowPitch +
           blitProperties.dstOffset.z * blitProperties.dstSlicePitch +
           row * blitProperties.dstRowPitch +
           slice * blitProperties.dstSlicePitch;
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandSourceBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice) {
    return blitProperties.srcGpuAddress + blitProperties.srcOffset.x + offset +
           blitProperties.srcOffset.y * blitProperties.srcRowPitch +
           blitProperties.srcOffset.z * blitProperties.srcSlicePitch +
           row * blitProperties.srcRowPitch +
           slice * blitProperties.srcSlicePitch;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsForBufferPerRow(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment) {
    uint64_t width = 1;
    uint64_t height = 1;

    PRINT_DEBUG_STRING(DebugManager.flags.PrintBlitDispatchDetails.get(), stdout,
                       "\nBlit dispatch with AuxTranslationDirection %u ", static_cast<uint32_t>(blitProperties.auxTranslationDirection));

    dispatchPreBlitCommand(linearStream);

    for (uint64_t slice = 0; slice < blitProperties.copySize.z; slice++) {
        for (uint64_t row = 0; row < blitProperties.copySize.y; row++) {
            uint64_t offset = 0;
            uint64_t sizeToBlit = blitProperties.copySize.x;
            while (sizeToBlit != 0) {
                if (sizeToBlit > getMaxBlitWidth(rootDeviceEnvironment)) {
                    // dispatch 2D blit: maxBlitWidth x (1 .. maxBlitHeight)
                    width = getMaxBlitWidth(rootDeviceEnvironment);
                    height = std::min((sizeToBlit / width), getMaxBlitHeight(rootDeviceEnvironment));
                } else {
                    // dispatch 1D blt: (1 .. maxBlitWidth) x 1
                    width = sizeToBlit;
                    height = 1;
                }

                {
                    auto bltCmd = GfxFamily::cmdInitXyCopyBlt;

                    bltCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(width));
                    bltCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(height));
                    bltCmd.setDestinationPitch(static_cast<uint32_t>(width));
                    bltCmd.setSourcePitch(static_cast<uint32_t>(width));

                    auto dstAddr = calculateBlitCommandDestinationBaseAddress(blitProperties, offset, row, slice);
                    auto srcAddr = calculateBlitCommandSourceBaseAddress(blitProperties, offset, row, slice);

                    PRINT_DEBUG_STRING(DebugManager.flags.PrintBlitDispatchDetails.get(), stdout,
                                       "\nBlit command. width: %u, height: %u, srcAddr: %#llx, dstAddr: %#llx ", width, height, srcAddr, dstAddr);

                    bltCmd.setDestinationBaseAddress(dstAddr);
                    bltCmd.setSourceBaseAddress(srcAddr);

                    appendBlitCommandsForBuffer(blitProperties, bltCmd, rootDeviceEnvironment);

                    auto bltStream = linearStream.getSpaceForCmd<typename GfxFamily::XY_COPY_BLT>();
                    *bltStream = bltCmd;
                }

                dispatchPostBlitCommand(linearStream);

                auto blitSize = width * height;
                sizeToBlit -= blitSize;
                offset += blitSize;
            }
        }
    }
}

template <typename GfxFamily>
template <size_t patternSize>
void BlitCommandsHelper<GfxFamily>::dispatchBlitMemoryFill(NEO::GraphicsAllocation *dstAlloc, uint64_t offset, uint32_t *pattern, LinearStream &linearStream, size_t size, const RootDeviceEnvironment &rootDeviceEnvironment, COLOR_DEPTH depth) {
    using XY_COLOR_BLT = typename GfxFamily::XY_COLOR_BLT;
    auto blitCmd = GfxFamily::cmdInitXyColorBlt;

    blitCmd.setFillColor(pattern);
    blitCmd.setColorDepth(depth);

    uint64_t sizeToFill = size / patternSize;
    while (sizeToFill != 0) {
        auto tmpCmd = blitCmd;
        tmpCmd.setDestinationBaseAddress(ptrOffset(dstAlloc->getGpuAddress(), static_cast<size_t>(offset)));
        uint64_t height = 0;
        uint64_t width = 0;
        if (sizeToFill <= getMaxBlitWidth(rootDeviceEnvironment)) {
            width = sizeToFill;
            height = 1;
        } else {
            width = getMaxBlitWidth(rootDeviceEnvironment);
            height = std::min((sizeToFill / width), getMaxBlitHeight(rootDeviceEnvironment));
            if (height > 1) {
                appendTilingEnable(tmpCmd);
            }
        }
        tmpCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(width));
        tmpCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(height));
        tmpCmd.setDestinationPitch(static_cast<uint32_t>(width));

        appendBlitCommandsForFillBuffer(dstAlloc, tmpCmd, rootDeviceEnvironment);

        auto cmd = linearStream.getSpaceForCmd<XY_COLOR_BLT>();
        *cmd = tmpCmd;
        auto blitSize = width * height;
        offset += (blitSize * patternSize);
        sizeToFill -= blitSize;
    }
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsRegion(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment) {
    auto srcSlicePitch = static_cast<uint32_t>(blitProperties.srcSlicePitch);
    auto dstSlicePitch = static_cast<uint32_t>(blitProperties.dstSlicePitch);

    UNRECOVERABLE_IF(blitProperties.copySize.x > BlitterConstants::maxBlitWidth || blitProperties.copySize.y > BlitterConstants::maxBlitHeight);
    auto bltCmd = GfxFamily::cmdInitXyCopyBlt;

    bltCmd.setSourceBaseAddress(blitProperties.srcGpuAddress);
    bltCmd.setDestinationBaseAddress(blitProperties.dstGpuAddress);

    bltCmd.setDestinationX1CoordinateLeft(static_cast<uint32_t>(blitProperties.dstOffset.x));
    bltCmd.setDestinationY1CoordinateTop(static_cast<uint32_t>(blitProperties.dstOffset.y));
    bltCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(blitProperties.dstOffset.x + blitProperties.copySize.x));
    bltCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(blitProperties.dstOffset.y + blitProperties.copySize.y));

    bltCmd.setSourceX1CoordinateLeft(static_cast<uint32_t>(blitProperties.srcOffset.x));
    bltCmd.setSourceY1CoordinateTop(static_cast<uint32_t>(blitProperties.srcOffset.y));

    appendBlitCommandsForBuffer(blitProperties, bltCmd, rootDeviceEnvironment);
    appendBlitCommandsForImages(blitProperties, bltCmd, rootDeviceEnvironment, srcSlicePitch, dstSlicePitch);
    appendColorDepth(blitProperties, bltCmd);
    appendSurfaceType(blitProperties, bltCmd);
    dispatchPreBlitCommand(linearStream);
    for (uint32_t i = 0; i < blitProperties.copySize.z; i++) {
        appendSliceOffsets(blitProperties, bltCmd, i, rootDeviceEnvironment, srcSlicePitch, dstSlicePitch);
        auto cmd = linearStream.getSpaceForCmd<typename GfxFamily::XY_COPY_BLT>();
        *cmd = bltCmd;
        dispatchPostBlitCommand(linearStream);
    }
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchDebugPauseCommands(LinearStream &commandStream, uint64_t debugPauseStateGPUAddress, DebugPauseState confirmationTrigger, DebugPauseState waitCondition) {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;

    MiFlushArgs args;
    args.commandWithPostSync = true;
    EncodeMiFlushDW<GfxFamily>::programMiFlushDw(commandStream, debugPauseStateGPUAddress, static_cast<uint32_t>(confirmationTrigger), args);

    EncodeSempahore<GfxFamily>::addMiSemaphoreWaitCommand(commandStream, debugPauseStateGPUAddress, static_cast<uint32_t>(waitCondition), MI_SEMAPHORE_WAIT::COMPARE_OPERATION::COMPARE_OPERATION_SAD_EQUAL_SDD);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getSizeForDebugPauseCommands() {
    return (EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite() + EncodeSempahore<GfxFamily>::getSizeMiSemaphoreWait()) * 2;
}

template <typename GfxFamily>
bool BlitCommandsHelper<GfxFamily>::useOneBlitCopyCommand(const Vec3<size_t> &copySize, uint32_t bytesPerPixel) {
    return (copySize.x / bytesPerPixel <= BlitterConstants::maxBlitWidth && copySize.y <= BlitterConstants::maxBlitHeight);
}

template <typename GfxFamily>
uint32_t BlitCommandsHelper<GfxFamily>::getAvailableBytesPerPixel(size_t copySize, uint32_t srcOrigin, uint32_t dstOrigin, size_t srcSize, size_t dstSize) {
    uint32_t bytesPerPixel = BlitterConstants::maxBytesPerPixel;
    while (bytesPerPixel > 1) {
        if (copySize % bytesPerPixel == 0 && srcSize % bytesPerPixel == 0 && dstSize % bytesPerPixel == 0) {
            if ((srcOrigin ? (srcOrigin % bytesPerPixel == 0) : true) && (dstOrigin ? (dstOrigin % bytesPerPixel == 0) : true)) {
                break;
            }
        }
        bytesPerPixel >>= 1;
    }
    return bytesPerPixel;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommands(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment) {

    if (blitProperties.blitDirection == BlitterConstants::BlitDirection::HostPtrToImage ||
        blitProperties.blitDirection == BlitterConstants::BlitDirection::ImageToHostPtr ||
        blitProperties.blitDirection == BlitterConstants::BlitDirection::ImageToImage) {
        dispatchBlitCommandsRegion(blitProperties, linearStream, rootDeviceEnvironment);
    } else {
        bool preferCopyBufferRegion = isCopyRegionPreferred(blitProperties.copySize, rootDeviceEnvironment);
        preferCopyBufferRegion ? dispatchBlitCommandsForBufferRegion(blitProperties, linearStream, rootDeviceEnvironment)
                               : dispatchBlitCommandsForBufferPerRow(blitProperties, linearStream, rootDeviceEnvironment);
    }
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandSourceBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice) {
    return blitProperties.srcGpuAddress + blitProperties.srcOffset.x +
           (blitProperties.srcOffset.y * blitProperties.srcRowPitch) +
           (blitProperties.srcSlicePitch * (slice + blitProperties.srcOffset.z));
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandDestinationBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice) {
    return blitProperties.dstGpuAddress + blitProperties.dstOffset.x +
           (blitProperties.dstOffset.y * blitProperties.dstRowPitch) +
           (blitProperties.dstSlicePitch * (slice + blitProperties.dstOffset.z));
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsForBufferRegion(const BlitProperties &blitProperties, LinearStream &linearStream, const RootDeviceEnvironment &rootDeviceEnvironment) {
    const auto maxWidthToCopy = getMaxBlitWidth(rootDeviceEnvironment);
    const auto maxHeightToCopy = getMaxBlitHeight(rootDeviceEnvironment);

    dispatchPreBlitCommand(linearStream);

    for (size_t slice = 0u; slice < blitProperties.copySize.z; ++slice) {
        auto srcAddress = calculateBlitCommandSourceBaseAddressCopyRegion(blitProperties, slice);
        auto dstAddress = calculateBlitCommandDestinationBaseAddressCopyRegion(blitProperties, slice);
        auto heightToCopy = blitProperties.copySize.y;

        while (heightToCopy > 0) {
            auto height = static_cast<uint32_t>(std::min(heightToCopy, static_cast<size_t>(maxHeightToCopy)));
            auto widthToCopy = blitProperties.copySize.x;

            while (widthToCopy > 0) {
                auto width = static_cast<uint32_t>(std::min(widthToCopy, static_cast<size_t>(maxWidthToCopy)));
                auto bltCmd = GfxFamily::cmdInitXyCopyBlt;

                bltCmd.setSourceBaseAddress(srcAddress);
                bltCmd.setDestinationBaseAddress(dstAddress);
                bltCmd.setDestinationX2CoordinateRight(width);
                bltCmd.setDestinationY2CoordinateBottom(height);
                bltCmd.setSourcePitch(static_cast<uint32_t>(blitProperties.srcRowPitch));
                bltCmd.setDestinationPitch(static_cast<uint32_t>(blitProperties.dstRowPitch));

                appendBlitCommandsForBuffer(blitProperties, bltCmd, rootDeviceEnvironment);

                auto cmd = linearStream.getSpaceForCmd<typename GfxFamily::XY_COPY_BLT>();
                *cmd = bltCmd;
                dispatchPostBlitCommand(linearStream);

                srcAddress += width;
                dstAddress += width;
                widthToCopy -= width;
            }

            heightToCopy -= height;
            srcAddress += (blitProperties.srcRowPitch - blitProperties.copySize.x);
            srcAddress += (blitProperties.srcRowPitch * (height - 1));
            dstAddress += (blitProperties.dstRowPitch - blitProperties.copySize.x);
            dstAddress += (blitProperties.dstRowPitch * (height - 1));
        }
    }
}

template <typename GfxFamily>
bool BlitCommandsHelper<GfxFamily>::isCopyRegionPreferred(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment) {
    bool preferCopyRegion = getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment) < getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
    return preferCopyRegion;
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getNumberOfBlitsForCopyRegion(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment) {
    auto maxWidthToCopy = getMaxBlitWidth(rootDeviceEnvironment);
    auto maxHeightToCopy = getMaxBlitHeight(rootDeviceEnvironment);
    auto xBlits = static_cast<size_t>(std::ceil(copySize.x / static_cast<double>(maxWidthToCopy)));
    auto yBlits = static_cast<size_t>(std::ceil(copySize.y / static_cast<double>(maxHeightToCopy)));
    auto zBlits = static_cast<size_t>(copySize.z);
    auto nBlits = xBlits * yBlits * zBlits;

    return nBlits;
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getNumberOfBlitsForCopyPerRow(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment) {
    size_t xBlits = 0u;
    uint64_t width = 1;
    uint64_t height = 1;
    uint64_t sizeToBlit = copySize.x;

    while (sizeToBlit != 0) {
        if (sizeToBlit > getMaxBlitWidth(rootDeviceEnvironment)) {
            // dispatch 2D blit: maxBlitWidth x (1 .. maxBlitHeight)
            width = getMaxBlitWidth(rootDeviceEnvironment);
            height = std::min((sizeToBlit / width), getMaxBlitHeight(rootDeviceEnvironment));

        } else {
            // dispatch 1D blt: (1 .. maxBlitWidth) x 1
            width = sizeToBlit;
            height = 1;
        }
        sizeToBlit -= (width * height);
        xBlits++;
    }

    auto yBlits = copySize.y;
    auto zBlits = copySize.z;
    auto nBlits = xBlits * yBlits * zBlits;

    return nBlits;
}

template <typename GfxFamily>
bool BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired() {
    return false;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendExtraMemoryProperties(typename GfxFamily::XY_COPY_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment) {}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendExtraMemoryProperties(typename GfxFamily::XY_COLOR_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment) {}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::encodeProfilingStartMmios(LinearStream &cmdStream, const TagNodeBase &timestampPacketNode) {
    auto timestampContextStartGpuAddress = TimestampPacketHelper::getContextStartGpuAddress(timestampPacketNode);
    auto timestampGlobalStartAddress = TimestampPacketHelper::getGlobalStartGpuAddress(timestampPacketNode);

    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, GP_THREAD_TIME_REG_ADDRESS_OFFSET_LOW, timestampContextStartGpuAddress);
    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, REG_GLOBAL_TIMESTAMP_LDW, timestampGlobalStartAddress);
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::encodeProfilingEndMmios(LinearStream &cmdStream, const TagNodeBase &timestampPacketNode) {
    auto timestampContextEndGpuAddress = TimestampPacketHelper::getContextEndGpuAddress(timestampPacketNode);
    auto timestampGlobalEndAddress = TimestampPacketHelper::getGlobalEndGpuAddress(timestampPacketNode);

    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, GP_THREAD_TIME_REG_ADDRESS_OFFSET_LOW, timestampContextEndGpuAddress);
    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, REG_GLOBAL_TIMESTAMP_LDW, timestampGlobalEndAddress);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getProfilingMmioCmdsSize() {
    return 4 * sizeof(typename GfxFamily::MI_STORE_REGISTER_MEM);
}
} // namespace NEO
