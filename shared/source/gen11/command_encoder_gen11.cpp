/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/gen11/hw_cmds_base.h"
#include "shared/source/gen11/reg_configs.h"

using Family = NEO::ICLFamily;

#include "shared/source/command_container/command_encoder.inl"
#include "shared/source/command_container/command_encoder_base.inl"
#include "shared/source/command_container/encode_compute_mode_bdw_plus.inl"

namespace NEO {

template <>
bool EncodeSurfaceState<Family>::doBindingTablePrefetch() {
    return false;
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
template struct EncodeMemoryPrefetch<Family>;
template struct EncodeWA<Family>;
} // namespace NEO
