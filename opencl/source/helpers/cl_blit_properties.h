/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/blit_commands_helper.h"

#include "opencl/source/built_ins/builtins_dispatch_builder.h"
#include "opencl/source/mem_obj/image.h"

#include "CL/cl.h"

namespace NEO {

struct ClBlitProperties {
    static BlitProperties constructProperties(BlitterConstants::BlitDirection blitDirection,
                                              CommandStreamReceiver &commandStreamReceiver,
                                              const BuiltinOpParams &builtinOpParams) {

        auto rootDeviceIndex = commandStreamReceiver.getRootDeviceIndex();
        auto clearColorAllocation = commandStreamReceiver.getClearColorAllocation();
        BlitProperties blitProperties{};

        if (BlitterConstants::BlitDirection::BufferToBuffer == blitDirection ||
            BlitterConstants::BlitDirection::ImageToImage == blitDirection) {
            auto dstOffset = builtinOpParams.dstOffset.x;
            auto srcOffset = builtinOpParams.srcOffset.x;
            GraphicsAllocation *dstAllocation = nullptr;
            GraphicsAllocation *srcAllocation = nullptr;

            if (!builtinOpParams.dstSvmAlloc) {
                dstOffset += builtinOpParams.dstMemObj->getOffset();
                srcOffset += builtinOpParams.srcMemObj->getOffset();
                dstAllocation = builtinOpParams.dstMemObj->getGraphicsAllocation(rootDeviceIndex);
                srcAllocation = builtinOpParams.srcMemObj->getGraphicsAllocation(rootDeviceIndex);
            } else {
                dstAllocation = builtinOpParams.dstSvmAlloc;
                srcAllocation = builtinOpParams.srcSvmAlloc;
                dstOffset += ptrDiff(builtinOpParams.dstPtr, dstAllocation->getGpuAddress());
                srcOffset += ptrDiff(builtinOpParams.srcPtr, srcAllocation->getGpuAddress());
            }

            blitProperties = BlitProperties::constructPropertiesForCopy(dstAllocation,
                                                                        srcAllocation,
                                                                        {dstOffset, builtinOpParams.dstOffset.y, builtinOpParams.dstOffset.z},
                                                                        {srcOffset, builtinOpParams.srcOffset.y, builtinOpParams.srcOffset.z},
                                                                        builtinOpParams.size,
                                                                        builtinOpParams.srcRowPitch, builtinOpParams.srcSlicePitch,
                                                                        builtinOpParams.dstRowPitch, builtinOpParams.dstSlicePitch, clearColorAllocation);

            if (BlitterConstants::BlitDirection::ImageToImage == blitDirection) {
                blitProperties.blitDirection = blitDirection;
                setBlitPropertiesForImage(blitProperties, builtinOpParams);
            }
            return blitProperties;
        }

        GraphicsAllocation *gpuAllocation = nullptr;
        Vec3<size_t> copyOffset = 0;

        void *hostPtr = nullptr;
        Vec3<size_t> hostPtrOffset = 0;

        uint64_t memObjGpuVa = 0;
        uint64_t hostAllocGpuVa = 0;

        GraphicsAllocation *hostAllocation = builtinOpParams.transferAllocation;

        Vec3<size_t> copySize = 0;
        size_t hostRowPitch = 0;
        size_t hostSlicePitch = 0;
        size_t gpuRowPitch = 0;
        size_t gpuSlicePitch = 0;

        if (BlitterConstants::BlitDirection::HostPtrToBuffer == blitDirection ||
            BlitterConstants::BlitDirection::HostPtrToImage == blitDirection) {
            // write buffer/image
            hostPtr = builtinOpParams.srcPtr;
            hostPtrOffset = builtinOpParams.srcOffset;
            copyOffset = builtinOpParams.dstOffset;

            memObjGpuVa = castToUint64(builtinOpParams.dstPtr);
            hostAllocGpuVa = castToUint64(builtinOpParams.srcPtr);

            if (builtinOpParams.dstSvmAlloc) {
                gpuAllocation = builtinOpParams.dstSvmAlloc;
                hostAllocation = builtinOpParams.srcSvmAlloc;
            } else {
                gpuAllocation = builtinOpParams.dstMemObj->getGraphicsAllocation(rootDeviceIndex);
                memObjGpuVa = (gpuAllocation->getGpuAddress() + builtinOpParams.dstMemObj->getOffset());
            }

            hostRowPitch = builtinOpParams.srcRowPitch;
            hostSlicePitch = builtinOpParams.srcSlicePitch;
            gpuRowPitch = builtinOpParams.dstRowPitch;
            gpuSlicePitch = builtinOpParams.dstSlicePitch;
            copySize = builtinOpParams.size;
        }

        if (BlitterConstants::BlitDirection::BufferToHostPtr == blitDirection ||
            BlitterConstants::BlitDirection::ImageToHostPtr == blitDirection) {
            // read buffer/image
            hostPtr = builtinOpParams.dstPtr;
            hostPtrOffset = builtinOpParams.dstOffset;
            copyOffset = builtinOpParams.srcOffset;

            memObjGpuVa = castToUint64(builtinOpParams.srcPtr);
            hostAllocGpuVa = castToUint64(builtinOpParams.dstPtr);

            if (builtinOpParams.srcSvmAlloc) {
                gpuAllocation = builtinOpParams.srcSvmAlloc;
                hostAllocation = builtinOpParams.dstSvmAlloc;
            } else {
                gpuAllocation = builtinOpParams.srcMemObj->getGraphicsAllocation(rootDeviceIndex);
                memObjGpuVa = (gpuAllocation->getGpuAddress() + builtinOpParams.srcMemObj->getOffset());
            }

            hostRowPitch = builtinOpParams.dstRowPitch;
            hostSlicePitch = builtinOpParams.dstSlicePitch;
            gpuRowPitch = builtinOpParams.srcRowPitch;
            gpuSlicePitch = builtinOpParams.srcSlicePitch;
            copySize = builtinOpParams.size;
        }

        UNRECOVERABLE_IF(BlitterConstants::BlitDirection::HostPtrToBuffer != blitDirection &&
                         BlitterConstants::BlitDirection::BufferToHostPtr != blitDirection &&
                         BlitterConstants::BlitDirection::HostPtrToImage != blitDirection &&
                         BlitterConstants::BlitDirection::ImageToHostPtr != blitDirection);

        blitProperties = BlitProperties::constructPropertiesForReadWrite(blitDirection, commandStreamReceiver, gpuAllocation,
                                                                         hostAllocation, hostPtr, memObjGpuVa, hostAllocGpuVa,
                                                                         hostPtrOffset, copyOffset, copySize,
                                                                         hostRowPitch, hostSlicePitch,
                                                                         gpuRowPitch, gpuSlicePitch);

        if (BlitterConstants::BlitDirection::HostPtrToImage == blitDirection ||
            BlitterConstants::BlitDirection::ImageToHostPtr == blitDirection) {
            setBlitPropertiesForImage(blitProperties, builtinOpParams);
        }

        return blitProperties;
    }

    static BlitterConstants::BlitDirection obtainBlitDirection(uint32_t commandType) {

        switch (commandType) {
        case CL_COMMAND_WRITE_BUFFER:
        case CL_COMMAND_WRITE_BUFFER_RECT:
            return BlitterConstants::BlitDirection::HostPtrToBuffer;
        case CL_COMMAND_READ_BUFFER:
        case CL_COMMAND_READ_BUFFER_RECT:
            return BlitterConstants::BlitDirection::BufferToHostPtr;
        case CL_COMMAND_COPY_BUFFER:
        case CL_COMMAND_COPY_BUFFER_RECT:
        case CL_COMMAND_SVM_MEMCPY:
            return BlitterConstants::BlitDirection::BufferToBuffer;
        case CL_COMMAND_WRITE_IMAGE:
            return BlitterConstants::BlitDirection::HostPtrToImage;
        case CL_COMMAND_READ_IMAGE:
            return BlitterConstants::BlitDirection::ImageToHostPtr;
        case CL_COMMAND_COPY_IMAGE:
            return BlitterConstants::BlitDirection::ImageToImage;
        default:
            UNRECOVERABLE_IF(true);
        }
    }

    static void adjustBlitPropertiesForImage(MemObj *memObj, Vec3<size_t> &size, size_t &bytesPerPixel, uint64_t &gpuAddress, size_t &rowPitch, size_t &slicePitch) {
        auto image = castToObject<Image>(memObj);
        const auto &imageDesc = image->getImageDesc();
        auto image_width = imageDesc.image_width;
        auto image_height = imageDesc.image_height;
        auto image_depth = imageDesc.image_depth;

        if (imageDesc.image_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
            image_depth = std::max(image_depth, imageDesc.image_array_size);
        }

        SurfaceOffsets surfaceOffsets;
        image->getSurfaceOffsets(surfaceOffsets);
        gpuAddress += surfaceOffsets.offset;
        size.x = image_width;
        size.y = image_height ? image_height : 1;
        size.z = image_depth ? image_depth : 1;
        bytesPerPixel = image->getSurfaceFormatInfo().surfaceFormat.ImageElementSizeInBytes;
        rowPitch = imageDesc.image_row_pitch;
        slicePitch = imageDesc.image_slice_pitch;
    }

    static void setBlitPropertiesForImage(BlitProperties &blitProperties, const BuiltinOpParams &builtinOpParams) {
        size_t srcRowPitch = builtinOpParams.srcRowPitch;
        size_t dstRowPitch = builtinOpParams.dstRowPitch;
        size_t srcSlicePitch = builtinOpParams.srcSlicePitch;
        size_t dstSlicePitch = builtinOpParams.dstSlicePitch;

        if (blitProperties.blitDirection == BlitterConstants::BlitDirection::ImageToHostPtr ||
            blitProperties.blitDirection == BlitterConstants::BlitDirection::ImageToImage) {
            adjustBlitPropertiesForImage(builtinOpParams.srcMemObj, blitProperties.srcSize, blitProperties.bytesPerPixel,
                                         blitProperties.srcGpuAddress, srcRowPitch, srcSlicePitch);
        }

        if (blitProperties.blitDirection == BlitterConstants::BlitDirection::HostPtrToImage ||
            blitProperties.blitDirection == BlitterConstants::BlitDirection::ImageToImage) {
            adjustBlitPropertiesForImage(builtinOpParams.dstMemObj, blitProperties.dstSize, blitProperties.bytesPerPixel,
                                         blitProperties.dstGpuAddress, dstRowPitch, dstSlicePitch);
        }

        blitProperties.srcRowPitch = srcRowPitch ? srcRowPitch : blitProperties.srcSize.x * blitProperties.bytesPerPixel;
        blitProperties.dstRowPitch = dstRowPitch ? dstRowPitch : blitProperties.dstSize.x * blitProperties.bytesPerPixel;
        blitProperties.srcSlicePitch = srcSlicePitch ? srcSlicePitch : blitProperties.srcSize.y * blitProperties.srcRowPitch;
        blitProperties.dstSlicePitch = dstSlicePitch ? dstSlicePitch : blitProperties.dstSize.y * blitProperties.dstRowPitch;
    }
};

} // namespace NEO
