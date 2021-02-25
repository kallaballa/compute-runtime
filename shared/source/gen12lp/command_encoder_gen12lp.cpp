/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/gen12lp/hw_cmds_base.h"
#include "shared/source/gen12lp/reg_configs.h"
#include "shared/source/helpers/preamble.h"

using Family = NEO::TGLLPFamily;

#include "shared/source/command_container/command_encoder.inl"
#include "shared/source/command_container/command_encoder_bdw_plus.inl"
#include "shared/source/command_container/encode_compute_mode_tgllp_plus.inl"
#include "shared/source/command_container/image_surface_state/compression_params_bdw_plus.inl"
#include "shared/source/command_container/image_surface_state/compression_params_tgllp_plus.inl"
#include "shared/source/command_stream/command_stream_receiver.h"

namespace NEO {
template <>
size_t EncodeWA<Family>::getAdditionalPipelineSelectSize(Device &device) {
    size_t size = 0;
    if (device.getDefaultEngine().commandStreamReceiver->isRcs()) {
        size += 2 * PreambleHelper<Family>::getCmdSizeForPipelineSelect(device.getHardwareInfo());
    }
    return size;
}

template <>
size_t EncodeStates<Family>::getAdjustStateComputeModeSize() {
    return sizeof(typename Family::STATE_COMPUTE_MODE);
}

template <>
void EncodeComputeMode<Family>::adjustComputeMode(LinearStream &csr, uint32_t numGrfRequired, void *const stateComputeModePtr, bool isMultiOsContextCapable) {
    STATE_COMPUTE_MODE *stateComputeMode = static_cast<STATE_COMPUTE_MODE *>(stateComputeModePtr);
    auto buffer = csr.getSpace(sizeof(STATE_COMPUTE_MODE));
    *reinterpret_cast<STATE_COMPUTE_MODE *>(buffer) = *stateComputeMode;
}

template <>
void EncodeWA<Family>::encodeAdditionalPipelineSelect(Device &device, LinearStream &stream, bool is3DPipeline) {
    if (device.getDefaultEngine().commandStreamReceiver->isRcs()) {
        PipelineSelectArgs args;
        args.is3DPipelineRequired = is3DPipeline;
        PreambleHelper<Family>::programPipelineSelect(&stream, args, device.getHardwareInfo());
    }
}

template <>
void EncodeSurfaceState<Family>::encodeExtraBufferParams(R_SURFACE_STATE *surfaceState, GraphicsAllocation *allocation, GmmHelper *gmmHelper,
                                                         bool isReadOnly, uint32_t numAvailableDevices, bool useGlobalAtomics, size_t numDevicesInContext) {
    const bool isL3Allowed = surfaceState->getMemoryObjectControlState() == gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER);
    if (isL3Allowed) {
        const bool isConstantSurface = allocation && allocation->getAllocationType() == GraphicsAllocation::AllocationType::CONSTANT_SURFACE;
        bool useL1 = isReadOnly || isConstantSurface;

        if (DebugManager.flags.ForceL1Caching.get() != 1) {
            useL1 = false;
        }

        if (useL1) {
            surfaceState->setMemoryObjectControlState(gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CONST));
        }
    }
}

template <>
bool EncodeSurfaceState<Family>::doBindingTablePrefetch() {
    return false;
}

template <>
void EncodeL3State<Family>::encode(CommandContainer &container, bool enableSLM) {
}

template struct EncodeDispatchKernel<Family>;
template struct EncodeStates<Family>;
template struct EncodeMath<Family>;
template struct EncodeMathMMIO<Family>;
template struct EncodeIndirectParams<Family>;
template struct EncodeSetMMIO<Family>;
template struct EncodeL3State<Family>;
template struct EncodeMediaInterfaceDescriptorLoad<Family>;
template struct EncodeStateBaseAddress<Family>;
template struct EncodeStoreMMIO<Family>;
template struct EncodeSurfaceState<Family>;
template struct EncodeAtomic<Family>;
template struct EncodeSempahore<Family>;
template struct EncodeBatchBufferStartOrEnd<Family>;
template struct EncodeMiFlushDW<Family>;
template struct EncodeWA<Family>;
template struct EncodeMemoryPrefetch<Family>;
template struct EncodeMiArbCheck<Family>;
} // namespace NEO
