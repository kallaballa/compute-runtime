/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/command_stream/stream_properties.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/pipeline_select_helper.h"
#include "shared/source/helpers/preamble_base.inl"

#include "reg_configs_common.h"

// L3 programming:
// All L3 Client Pool: 320KB
// URB Pool: 64KB
// Use Full ways: true
// SLM: reserved (always enabled)

namespace NEO {

template <>
void PreambleHelper<Family>::programPipelineSelect(LinearStream *pCommandStream,
                                                   const PipelineSelectArgs &pipelineSelectArgs,
                                                   const HardwareInfo &hwInfo) {

    using PIPELINE_SELECT = typename Family::PIPELINE_SELECT;

    PIPELINE_SELECT cmd = Family::cmdInitPipelineSelect;

    if (DebugManager.flags.CleanStateInPreamble.get()) {
        auto pCmd = pCommandStream->getSpaceForCmd<PIPELINE_SELECT>();
        cmd.setPipelineSelection(PIPELINE_SELECT::PIPELINE_SELECTION_3D);
        *pCmd = cmd;

        auto pipeControl = Family::cmdInitPipeControl;
        pipeControl.setStateCacheInvalidationEnable(true);
        auto pipeControlBuffer = pCommandStream->getSpaceForCmd<PIPE_CONTROL>();
        *pipeControlBuffer = pipeControl;
    }

    auto pCmd = pCommandStream->getSpaceForCmd<PIPELINE_SELECT>();

    auto mask = pipelineSelectEnablePipelineSelectMaskBits |
                pipelineSelectMediaSamplerDopClockGateMaskBits |
                pipelineSelectSystolicModeEnableMaskBits;

    cmd.setMaskBits(mask);
    cmd.setPipelineSelection(PIPELINE_SELECT::PIPELINE_SELECTION_GPGPU);
    cmd.setMediaSamplerDopClockGateEnable(!pipelineSelectArgs.mediaSamplerRequired);
    cmd.setSystolicModeEnable(pipelineSelectArgs.specialPipelineSelectMode);

    if (DebugManager.flags.OverrideSystolicPipelineSelect.get() != -1) {
        cmd.setSystolicModeEnable(DebugManager.flags.OverrideSystolicPipelineSelect.get());
    }

    *pCmd = cmd;

    if (DebugManager.flags.CleanStateInPreamble.get()) {
        auto pipeControl = Family::cmdInitPipeControl;
        pipeControl.setStateCacheInvalidationEnable(true);
        auto pipeControlBuffer = pCommandStream->getSpaceForCmd<PIPE_CONTROL>();
        *pipeControlBuffer = pipeControl;
    }
}

template <>
void PreambleHelper<Family>::addPipeControlBeforeVfeCmd(LinearStream *pCommandStream, const HardwareInfo *hwInfo, EngineGroupType engineGroupType) {
}

template <>
void PreambleHelper<Family>::programL3(LinearStream *pCommandStream, uint32_t l3Config) {
}

template <>
uint32_t PreambleHelper<Family>::getUrbEntryAllocationSize() {
    return 0u;
}
template <>
void PreambleHelper<Family>::appendProgramVFEState(const HardwareInfo &hwInfo, const StreamProperties &streamProperties, uint32_t additionalKernelExecInfo, void *cmd);

template <>
void *PreambleHelper<Family>::getSpaceForVfeState(LinearStream *pCommandStream,
                                                  const HardwareInfo &hwInfo,
                                                  EngineGroupType engineGroupType) {
    using CFE_STATE = typename Family::CFE_STATE;
    return pCommandStream->getSpace(sizeof(CFE_STATE));
}

template <>
void PreambleHelper<Family>::programVfeState(void *pVfeState,
                                             const HardwareInfo &hwInfo,
                                             uint32_t scratchSize,
                                             uint64_t scratchAddress,
                                             uint32_t maxFrontEndThreads,
                                             uint32_t additionalKernelExecInfo,
                                             const StreamProperties &streamProperties) {
    using CFE_STATE = typename Family::CFE_STATE;

    auto cfeState = reinterpret_cast<CFE_STATE *>(pVfeState);
    CFE_STATE cmd = Family::cmdInitCfeState;

    cmd.setNumberOfWalkers(1);

    uint32_t lowAddress = uint32_t(0xFFFFFFFF & scratchAddress);
    cmd.setScratchSpaceBuffer(lowAddress);
    cmd.setMaximumNumberOfThreads(maxFrontEndThreads);
    appendProgramVFEState(hwInfo, streamProperties, additionalKernelExecInfo, &cmd);

    if (DebugManager.flags.CFENumberOfWalkers.get() != -1) {
        cmd.setNumberOfWalkers(DebugManager.flags.CFENumberOfWalkers.get());
    }
    if (DebugManager.flags.CFEMaximumNumberOfThreads.get() != -1) {
        cmd.setMaximumNumberOfThreads(DebugManager.flags.CFEMaximumNumberOfThreads.get());
    }
    if (DebugManager.flags.CFEOverDispatchControl.get() != -1) {
        cmd.setOverDispatchControl(static_cast<typename CFE_STATE::OVER_DISPATCH_CONTROL>(DebugManager.flags.CFEOverDispatchControl.get()));
    }
    if (DebugManager.flags.CFEFusedEUDispatch.get() != -1) {
        cmd.setFusedEuDispatch(DebugManager.flags.CFEFusedEUDispatch.get());
    }
    if (DebugManager.flags.CFELargeGRFThreadAdjustDisable.get() != -1) {
        cmd.setLargeGRFThreadAdjustDisable(DebugManager.flags.CFELargeGRFThreadAdjustDisable.get());
    }

    *cfeState = cmd;
}

template <>
uint64_t PreambleHelper<Family>::getScratchSpaceAddressOffsetForVfeState(LinearStream *pCommandStream, void *pVfeState) {
    return 0;
}

template <>
size_t PreambleHelper<Family>::getVFECommandsSize() {
    using CFE_STATE = typename Family::CFE_STATE;
    return sizeof(CFE_STATE);
}

template <>
uint32_t PreambleHelper<Family>::getL3Config(const HardwareInfo &hwInfo, bool useSLM) {
    return 0u;
}

template <>
const uint32_t L3CNTLRegisterOffset<Family>::registerOffset = std::numeric_limits<uint32_t>::max();

} // namespace NEO
