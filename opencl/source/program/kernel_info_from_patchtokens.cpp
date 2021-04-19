/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/program/kernel_info_from_patchtokens.h"

#include "shared/source/device_binary_format/patchtokens_decoder.h"
#include "shared/source/kernel/kernel_descriptor_from_patchtokens.h"

#include "opencl/source/program/kernel_info.h"

#include <cstring>

namespace NEO {

using namespace iOpenCL;

template <typename T>
inline void storeTokenIfNotNull(KernelInfo &kernelInfo, T *token) {
    if (token != nullptr) {
        kernelInfo.storePatchToken(token);
    }
}
template <typename T>
inline uint32_t getOffset(T *token) {
    if (token != nullptr) {
        return token->Offset;
    }
    return undefined<uint32_t>;
}
void populateKernelInfoArgMetadata(KernelInfo &dstKernelInfoArg, const SPatchKernelArgumentInfo *src) {
    if (nullptr == src) {
        return;
    }

    uint32_t argNum = src->ArgumentNumber;

    auto inlineData = PatchTokenBinary::getInlineData(src);

    auto metadataExtended = std::make_unique<ArgTypeMetadataExtended>();
    metadataExtended->addressQualifier = parseLimitedString(inlineData.addressQualifier.begin(), inlineData.addressQualifier.size());
    metadataExtended->accessQualifier = parseLimitedString(inlineData.accessQualifier.begin(), inlineData.accessQualifier.size());
    metadataExtended->argName = parseLimitedString(inlineData.argName.begin(), inlineData.argName.size());

    auto argTypeFull = parseLimitedString(inlineData.typeName.begin(), inlineData.typeName.size());
    const char *argTypeDelim = strchr(argTypeFull.data(), ';');
    if (nullptr == argTypeDelim) {
        argTypeDelim = argTypeFull.data() + argTypeFull.size();
    }
    metadataExtended->type = std::string(static_cast<const char *>(argTypeFull.data()), argTypeDelim).c_str();
    metadataExtended->typeQualifiers = parseLimitedString(inlineData.typeQualifiers.begin(), inlineData.typeQualifiers.size());

    ArgTypeTraits metadata = {};
    metadata.accessQualifier = KernelArgMetadata::parseAccessQualifier(metadataExtended->accessQualifier);
    metadata.addressQualifier = KernelArgMetadata::parseAddressSpace(metadataExtended->addressQualifier);
    metadata.typeQualifiers = KernelArgMetadata::parseTypeQualifiers(metadataExtended->typeQualifiers);

    dstKernelInfoArg.storeArgInfo(argNum, metadata, std::move(metadataExtended));
}

void populateKernelInfoArg(KernelInfo &dstKernelInfo, KernelArgInfo &dstKernelInfoArg, const PatchTokenBinary::KernelArgFromPatchtokens &src) {
    populateKernelInfoArgMetadata(dstKernelInfo, src.argInfo);
    if (src.objectArg != nullptr) {
        switch (src.objectArg->Token) {
        default:
            UNRECOVERABLE_IF(PATCH_TOKEN_IMAGE_MEMORY_OBJECT_KERNEL_ARGUMENT != src.objectArg->Token);
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchImageMemoryObjectKernelArgument *>(src.objectArg));
            break;
        case PATCH_TOKEN_SAMPLER_KERNEL_ARGUMENT:
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchSamplerKernelArgument *>(src.objectArg));
            break;
        case PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT:
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchGlobalMemoryObjectKernelArgument *>(src.objectArg));
            break;
        case PATCH_TOKEN_STATELESS_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT:
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchStatelessGlobalMemoryObjectKernelArgument *>(src.objectArg));
            break;
        case PATCH_TOKEN_STATELESS_CONSTANT_MEMORY_OBJECT_KERNEL_ARGUMENT:
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchStatelessConstantMemoryObjectKernelArgument *>(src.objectArg));
            break;
        case PATCH_TOKEN_STATELESS_DEVICE_QUEUE_KERNEL_ARGUMENT:
            dstKernelInfo.storeKernelArgument(reinterpret_cast<const SPatchStatelessDeviceQueueKernelArgument *>(src.objectArg));
            break;
        }
    }

    switch (src.objectType) {
    default:
        UNRECOVERABLE_IF(PatchTokenBinary::ArgObjectType::None != src.objectType);
        break;
    case PatchTokenBinary::ArgObjectType::Buffer:
        dstKernelInfoArg.offsetBufferOffset = getOffset(src.metadata.buffer.bufferOffset);
        dstKernelInfoArg.pureStatefulBufferAccess = (src.metadata.buffer.pureStateful != nullptr);
        break;
    case PatchTokenBinary::ArgObjectType::Image:
        dstKernelInfoArg.offsetImgWidth = getOffset(src.metadata.image.width);
        dstKernelInfoArg.offsetImgHeight = getOffset(src.metadata.image.height);
        dstKernelInfoArg.offsetImgDepth = getOffset(src.metadata.image.depth);
        dstKernelInfoArg.offsetChannelDataType = getOffset(src.metadata.image.channelDataType);
        dstKernelInfoArg.offsetChannelOrder = getOffset(src.metadata.image.channelOrder);
        dstKernelInfoArg.offsetArraySize = getOffset(src.metadata.image.arraySize);
        dstKernelInfoArg.offsetNumSamples = getOffset(src.metadata.image.numSamples);
        dstKernelInfoArg.offsetNumMipLevels = getOffset(src.metadata.image.numMipLevels);
        dstKernelInfoArg.offsetFlatBaseOffset = getOffset(src.metadata.image.flatBaseOffset);
        dstKernelInfoArg.offsetFlatWidth = getOffset(src.metadata.image.flatWidth);
        dstKernelInfoArg.offsetFlatHeight = getOffset(src.metadata.image.flatHeight);
        dstKernelInfoArg.offsetFlatPitch = getOffset(src.metadata.image.flatPitch);
        break;
    case PatchTokenBinary::ArgObjectType::Sampler:
        dstKernelInfoArg.offsetSamplerSnapWa = getOffset(src.metadata.sampler.coordinateSnapWaRequired);
        dstKernelInfoArg.offsetSamplerAddressingMode = getOffset(src.metadata.sampler.addressMode);
        dstKernelInfoArg.offsetSamplerNormalizedCoords = getOffset(src.metadata.sampler.normalizedCoords);
        break;
    case PatchTokenBinary::ArgObjectType::Slm:
        dstKernelInfoArg.slmAlignment = src.metadata.slm.token->SourceOffset;
        break;
    }

    switch (src.objectTypeSpecialized) {
    default:
        UNRECOVERABLE_IF(PatchTokenBinary::ArgObjectTypeSpecialized::None != src.objectTypeSpecialized);
        break;
    case PatchTokenBinary::ArgObjectTypeSpecialized::Vme:
        dstKernelInfoArg.offsetVmeMbBlockType = getOffset(src.metadataSpecialized.vme.mbBlockType);
        dstKernelInfoArg.offsetVmeSubpixelMode = getOffset(src.metadataSpecialized.vme.subpixelMode);
        dstKernelInfoArg.offsetVmeSadAdjustMode = getOffset(src.metadataSpecialized.vme.sadAdjustMode);
        dstKernelInfoArg.offsetVmeSearchPathType = getOffset(src.metadataSpecialized.vme.searchPathType);
        break;
    }

    for (auto &byValArg : src.byValMap) {
        dstKernelInfo.storeKernelArgument(byValArg);
    }

    dstKernelInfoArg.offsetObjectId = getOffset(src.objectId);
}

void populateKernelInfo(KernelInfo &dst, const PatchTokenBinary::KernelFromPatchtokens &src, uint32_t gpuPointerSizeInBytes) {
    UNRECOVERABLE_IF(nullptr == src.header);

    dst.kernelDescriptor.kernelMetadata.kernelName = std::string(src.name.begin(), src.name.end()).c_str();
    dst.heapInfo.DynamicStateHeapSize = src.header->DynamicStateHeapSize;
    dst.heapInfo.GeneralStateHeapSize = src.header->GeneralStateHeapSize;
    dst.heapInfo.SurfaceStateHeapSize = src.header->SurfaceStateHeapSize;
    dst.heapInfo.KernelHeapSize = src.header->KernelHeapSize;
    dst.heapInfo.KernelUnpaddedSize = src.header->KernelUnpaddedSize;
    dst.shaderHashCode = src.header->ShaderHashCode;

    dst.heapInfo.pKernelHeap = src.isa.begin();
    dst.heapInfo.pGsh = src.heaps.generalState.begin();
    dst.heapInfo.pDsh = src.heaps.dynamicState.begin();
    dst.heapInfo.pSsh = src.heaps.surfaceState.begin();

    storeTokenIfNotNull(dst, src.tokens.executionEnvironment);
    dst.usesSsh = src.tokens.bindingTableState && (src.tokens.bindingTableState->Count > 0);
    dst.kernelDescriptor.kernelAttributes.slmInlineSize = src.tokens.allocateLocalSurface ? src.tokens.allocateLocalSurface->TotalInlineLocalMemorySize : 0U;

    dst.kernelArgInfo.resize(src.tokens.kernelArgs.size());

    for (size_t i = 0U; i < src.tokens.kernelArgs.size(); ++i) {
        auto &decodedKernelArg = src.tokens.kernelArgs[i];
        auto &kernelInfoArg = dst.kernelArgInfo[i];
        populateKernelInfoArg(dst, kernelInfoArg, decodedKernelArg);
    }

    if (nullptr != src.tokens.allocateSyncBuffer) {
        dst.usesSsh = true;
    }
    if (nullptr != src.tokens.allocateSystemThreadSurface) {
        dst.usesSsh = true;
    }

    dst.isVmeWorkload = dst.isVmeWorkload || (src.tokens.inlineVmeSamplerInfo != nullptr);
    dst.systemKernelOffset = src.tokens.stateSip ? src.tokens.stateSip->SystemKernelOffset : 0U;

    for (auto &childSimdSize : src.tokens.crossThreadPayloadArgs.childBlockSimdSize) {
        dst.childrenKernelsIdOffset.push_back({childSimdSize->ArgumentNumber, childSimdSize->Offset});
    }

    if (src.tokens.gtpinInfo) {
        dst.igcInfoForGtpin = reinterpret_cast<const gtpin::igc_info_t *>(src.tokens.gtpinInfo + 1);
    }

    dst.gpuPointerSize = gpuPointerSizeInBytes;

    if (useKernelDescriptor) {
        populateKernelDescriptor(dst.kernelDescriptor, src, gpuPointerSizeInBytes);
    }

    if (dst.kernelDescriptor.kernelAttributes.crossThreadDataSize) {
        dst.crossThreadData = new char[dst.kernelDescriptor.kernelAttributes.crossThreadDataSize];
        memset(dst.crossThreadData, 0x00, dst.kernelDescriptor.kernelAttributes.crossThreadDataSize);
    }
}

} // namespace NEO
