/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/command_stream_receiver_hw.h"
#include "shared/source/helpers/state_compute_mode_helper.h"

#include "pipe_control_args.h"

namespace NEO {
template <typename GfxFamily>
void CommandStreamReceiverHw<GfxFamily>::programComputeMode(LinearStream &stream, DispatchFlags &dispatchFlags) {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;
    if (isComputeModeNeeded()) {
        programAdditionalPipelineSelect(stream, dispatchFlags.pipelineSelectArgs, true);
        this->lastSentCoherencyRequest = static_cast<int8_t>(dispatchFlags.requiresCoherency);

        auto stateComputeMode = GfxFamily::cmdInitStateComputeMode;
        adjustThreadArbitionPolicy(&stateComputeMode);
        EncodeStates<GfxFamily>::adjustStateComputeMode(stream, dispatchFlags.numGrfRequired, &stateComputeMode, isMultiOsContextCapable(), dispatchFlags.requiresCoherency,
                                                        dispatchFlags.useGlobalAtomics, dispatchFlags.areMultipleSubDevicesInContext);

        if (csrSizeRequestFlags.hasSharedHandles) {
            auto pc = stream.getSpaceForCmd<PIPE_CONTROL>();
            *pc = GfxFamily::cmdInitPipeControl;
        }
        programAdditionalPipelineSelect(stream, dispatchFlags.pipelineSelectArgs, false);
    }
}

template <>
inline bool CommandStreamReceiverHw<Family>::isComputeModeNeeded() const {
    return csrSizeRequestFlags.coherencyRequestChanged || csrSizeRequestFlags.hasSharedHandles || csrSizeRequestFlags.numGrfRequiredChanged ||
           StateComputeModeHelper<Family>::isStateComputeModeRequired(csrSizeRequestFlags, this->lastSentThreadArbitrationPolicy != this->requiredThreadArbitrationPolicy);
}

template <>
inline void CommandStreamReceiverHw<Family>::addPipeControlBeforeStateBaseAddress(LinearStream &commandStream) {
    PipeControlArgs args(true);
    args.textureCacheInvalidationEnable = true;
    args.hdcPipelineFlush = true;
    addPipeControlCmd(commandStream, args);
}
} // namespace NEO
