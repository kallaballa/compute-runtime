/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/device/device.h"
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/bindless_heaps_helper.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/local_id_gen.h"
#include "shared/source/helpers/preamble.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/simd_helper.h"
#include "shared/source/helpers/string.h"
#include "shared/source/image/image_surface_state.h"
#include "shared/source/kernel/dispatch_kernel_encoder_interface.h"
#include "shared/source/kernel/kernel_descriptor.h"

#include <algorithm>

namespace NEO {

template <typename Family>
uint32_t EncodeStates<Family>::copySamplerState(IndirectHeap *dsh,
                                                uint32_t samplerStateOffset,
                                                uint32_t samplerCount,
                                                uint32_t borderColorOffset,
                                                const void *fnDynamicStateHeap,
                                                BindlessHeapsHelper *bindlessHeapHelper) {
    auto sizeSamplerState = sizeof(SAMPLER_STATE) * samplerCount;
    auto borderColorSize = samplerStateOffset - borderColorOffset;

    SAMPLER_STATE *dstSamplerState = nullptr;
    uint32_t samplerStateOffsetInDsh = 0;

    dsh->align(EncodeStates<Family>::alignIndirectStatePointer);
    uint32_t borderColorOffsetInDsh = 0;
    if (!ApiSpecificConfig::getBindlessConfiguration()) {
        borderColorOffsetInDsh = static_cast<uint32_t>(dsh->getUsed());
        auto borderColor = dsh->getSpace(borderColorSize);

        memcpy_s(borderColor, borderColorSize, ptrOffset(fnDynamicStateHeap, borderColorOffset),
                 borderColorSize);

        dsh->align(INTERFACE_DESCRIPTOR_DATA::SAMPLERSTATEPOINTER_ALIGN_SIZE);
        samplerStateOffsetInDsh = static_cast<uint32_t>(dsh->getUsed());

        dstSamplerState = reinterpret_cast<SAMPLER_STATE *>(dsh->getSpace(sizeSamplerState));
    } else {
        auto borderColor = reinterpret_cast<const SAMPLER_BORDER_COLOR_STATE *>(ptrOffset(fnDynamicStateHeap, borderColorOffset));
        if (borderColor->getBorderColorRed() != 0.0f ||
            borderColor->getBorderColorGreen() != 0.0f ||
            borderColor->getBorderColorBlue() != 0.0f ||
            (borderColor->getBorderColorAlpha() != 0.0f && borderColor->getBorderColorAlpha() != 1.0f)) {
            UNRECOVERABLE_IF(true);
        } else if (borderColor->getBorderColorAlpha() == 0.0f) {
            borderColorOffsetInDsh = bindlessHeapHelper->getDefaultBorderColorOffset();
        } else {
            borderColorOffsetInDsh = bindlessHeapHelper->getAlphaBorderColorOffset();
        }
        dsh->align(INTERFACE_DESCRIPTOR_DATA::SAMPLERSTATEPOINTER_ALIGN_SIZE);
        auto samplerStateInDsh = bindlessHeapHelper->allocateSSInHeap(sizeSamplerState, nullptr, BindlessHeapsHelper::BindlesHeapType::GLOBAL_DSH);
        dstSamplerState = reinterpret_cast<SAMPLER_STATE *>(samplerStateInDsh.ssPtr);
        samplerStateOffsetInDsh = static_cast<uint32_t>(samplerStateInDsh.surfaceStateOffset);
    }

    auto srcSamplerState = reinterpret_cast<const SAMPLER_STATE *>(ptrOffset(fnDynamicStateHeap, samplerStateOffset));
    SAMPLER_STATE state = {};
    for (uint32_t i = 0; i < samplerCount; i++) {
        state = srcSamplerState[i];
        state.setIndirectStatePointer(static_cast<uint32_t>(borderColorOffsetInDsh));
        dstSamplerState[i] = state;
    }

    return samplerStateOffsetInDsh;
} // namespace NEO

template <typename Family>
size_t EncodeStates<Family>::getAdjustStateComputeModeSize() {
    return 0;
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeMulRegVal(CommandContainer &container, uint32_t offset, uint32_t val, uint64_t dstAddress) {
    int logLws = 0;
    int i = val;
    while (val >> logLws) {
        logLws++;
    }

    EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R0, offset);
    EncodeSetMMIO<Family>::encodeIMM(container, CS_GPR_R1, 0, true);

    i = 0;
    while (i < logLws) {
        if (val & (1 << i)) {
            EncodeMath<Family>::addition(container, AluRegisters::R_1,
                                         AluRegisters::R_0, AluRegisters::R_2);
            EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R1, CS_GPR_R2);
        }
        EncodeMath<Family>::addition(container, AluRegisters::R_0,
                                     AluRegisters::R_0, AluRegisters::R_2);
        EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R0, CS_GPR_R2);
        i++;
    }
    EncodeStoreMMIO<Family>::encode(*container.getCommandStream(), CS_GPR_R1, dstAddress);
}

/*
 * Compute *firstOperand > secondOperand and store the result in
 * MI_PREDICATE_RESULT where  firstOperand is an device memory address.
 *
 * To calculate the "greater than" operation in the device,
 * (secondOperand - *firstOperand) is used, and if the carry flag register is
 * set, then (*firstOperand) is greater than secondOperand.
 */
template <typename Family>
void EncodeMathMMIO<Family>::encodeGreaterThanPredicate(CommandContainer &container, uint64_t firstOperand, uint32_t secondOperand) {
    EncodeSetMMIO<Family>::encodeMEM(container, CS_GPR_R0, firstOperand);
    EncodeSetMMIO<Family>::encodeIMM(container, CS_GPR_R1, secondOperand, true);

    /* CS_GPR_R* registers map to AluRegisters::R_* registers */
    EncodeMath<Family>::greaterThan(container, AluRegisters::R_0,
                                    AluRegisters::R_1, AluRegisters::R_2);

    EncodeSetMMIO<Family>::encodeREG(container, CS_PREDICATE_RESULT, CS_GPR_R2);
}

/*
 * Compute bitwise AND between a register value from regOffset and immVal
 * and store it into dstAddress.
 */
template <typename Family>
void EncodeMathMMIO<Family>::encodeBitwiseAndVal(CommandContainer &container, uint32_t regOffset, uint32_t immVal, uint64_t dstAddress) {
    EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R0, regOffset);
    EncodeSetMMIO<Family>::encodeIMM(container, CS_GPR_R1, immVal, true);
    EncodeMath<Family>::bitwiseAnd(container, AluRegisters::R_0,
                                   AluRegisters::R_1,
                                   AluRegisters::R_2);
    EncodeStoreMMIO<Family>::encode(*container.getCommandStream(),
                                    CS_GPR_R2, dstAddress);
}

/*
 * encodeAlu() performs operations that leave a state including the result of
 * an operation such as the carry flag, and the accu flag with subtraction and
 * addition result.
 *
 * Parameter "postOperationStateRegister" is the ALU register with the result
 * from the operation that the function caller is interested in obtaining.
 *
 * Parameter "finalResultRegister" is the final destination register where
 * data from "postOperationStateRegister" will be copied.
 */
template <typename Family>
void EncodeMathMMIO<Family>::encodeAlu(MI_MATH_ALU_INST_INLINE *pAluParam, AluRegisters srcA, AluRegisters srcB, AluRegisters op, AluRegisters finalResultRegister, AluRegisters postOperationStateRegister) {
    MI_MATH_ALU_INST_INLINE aluParam;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_LOAD);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(AluRegisters::R_SRCA);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(srcA);
    *pAluParam = aluParam;
    pAluParam++;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_LOAD);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(AluRegisters::R_SRCB);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(srcB);
    *pAluParam = aluParam;
    pAluParam++;

    /* Order of operation: Operand1 <ALUOpcode> Operand2 */
    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(op);
    aluParam.DW0.BitField.Operand1 = 0;
    aluParam.DW0.BitField.Operand2 = 0;
    *pAluParam = aluParam;
    pAluParam++;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_STORE);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(finalResultRegister);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(postOperationStateRegister);
    *pAluParam = aluParam;
    pAluParam++;
}

template <typename Family>
uint32_t *EncodeMath<Family>::commandReserve(CommandContainer &container) {
    size_t size = sizeof(MI_MATH) + sizeof(MI_MATH_ALU_INST_INLINE) * NUM_ALU_INST_FOR_READ_MODIFY_WRITE;

    auto cmd = reinterpret_cast<uint32_t *>(container.getCommandStream()->getSpace(size));
    MI_MATH mathBuffer;
    mathBuffer.DW0.Value = 0x0;
    mathBuffer.DW0.BitField.InstructionType = MI_MATH::COMMAND_TYPE_MI_COMMAND;
    mathBuffer.DW0.BitField.InstructionOpcode = MI_MATH::MI_COMMAND_OPCODE_MI_MATH;
    mathBuffer.DW0.BitField.DwordLength = NUM_ALU_INST_FOR_READ_MODIFY_WRITE - 1;
    *reinterpret_cast<MI_MATH *>(cmd) = mathBuffer;
    cmd++;

    return cmd;
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeAluAdd(MI_MATH_ALU_INST_INLINE *pAluParam,
                                          AluRegisters firstOperandRegister,
                                          AluRegisters secondOperandRegister,
                                          AluRegisters finalResultRegister) {
    encodeAlu(pAluParam, firstOperandRegister, secondOperandRegister, AluRegisters::OPCODE_ADD, finalResultRegister, AluRegisters::R_ACCU);
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeAluSubStoreCarry(MI_MATH_ALU_INST_INLINE *pAluParam, AluRegisters regA, AluRegisters regB, AluRegisters finalResultRegister) {
    /* regB is subtracted from regA */
    encodeAlu(pAluParam, regA, regB, AluRegisters::OPCODE_SUB, finalResultRegister, AluRegisters::R_CF);
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeAluAnd(MI_MATH_ALU_INST_INLINE *pAluParam,
                                          AluRegisters firstOperandRegister,
                                          AluRegisters secondOperandRegister,
                                          AluRegisters finalResultRegister) {
    encodeAlu(pAluParam, firstOperandRegister, secondOperandRegister, AluRegisters::OPCODE_AND, finalResultRegister, AluRegisters::R_ACCU);
}

/*
 * greaterThan() tests if firstOperandRegister is greater than
 * secondOperandRegister.
 */
template <typename Family>
void EncodeMath<Family>::greaterThan(CommandContainer &container,
                                     AluRegisters firstOperandRegister,
                                     AluRegisters secondOperandRegister,
                                     AluRegisters finalResultRegister) {
    uint32_t *cmd = EncodeMath<Family>::commandReserve(container);

    /* firstOperandRegister will be subtracted from secondOperandRegister */
    EncodeMathMMIO<Family>::encodeAluSubStoreCarry(reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(cmd),
                                                   secondOperandRegister,
                                                   firstOperandRegister,
                                                   finalResultRegister);
}

template <typename Family>
void EncodeMath<Family>::addition(CommandContainer &container,
                                  AluRegisters firstOperandRegister,
                                  AluRegisters secondOperandRegister,
                                  AluRegisters finalResultRegister) {
    uint32_t *cmd = EncodeMath<Family>::commandReserve(container);

    EncodeMathMMIO<Family>::encodeAluAdd(reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(cmd),
                                         firstOperandRegister,
                                         secondOperandRegister,
                                         finalResultRegister);
}

template <typename Family>
void EncodeMath<Family>::bitwiseAnd(CommandContainer &container,
                                    AluRegisters firstOperandRegister,
                                    AluRegisters secondOperandRegister,
                                    AluRegisters finalResultRegister) {
    uint32_t *cmd = EncodeMath<Family>::commandReserve(container);

    EncodeMathMMIO<Family>::encodeAluAnd(reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(cmd),
                                         firstOperandRegister,
                                         secondOperandRegister,
                                         finalResultRegister);
}

template <typename Family>
inline void EncodeSetMMIO<Family>::encodeIMM(CommandContainer &container, uint32_t offset, uint32_t data, bool remap) {
    LriHelper<Family>::program(container.getCommandStream(),
                               offset,
                               data,
                               remap);
}

template <typename Family>
void EncodeSetMMIO<Family>::encodeMEM(CommandContainer &container, uint32_t offset, uint64_t address) {
    MI_LOAD_REGISTER_MEM cmd = Family::cmdInitLoadRegisterMem;
    cmd.setRegisterAddress(offset);
    cmd.setMemoryAddress(address);
    remapOffset(&cmd);

    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_LOAD_REGISTER_MEM>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSetMMIO<Family>::encodeREG(CommandContainer &container, uint32_t dstOffset, uint32_t srcOffset) {
    MI_LOAD_REGISTER_REG cmd = Family::cmdInitLoadRegisterReg;
    cmd.setSourceRegisterAddress(srcOffset);
    cmd.setDestinationRegisterAddress(dstOffset);
    remapOffset(&cmd);
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_LOAD_REGISTER_REG>();
    *buffer = cmd;
}

template <typename Family>
void EncodeStoreMMIO<Family>::encode(LinearStream &csr, uint32_t offset, uint64_t address) {
    MI_STORE_REGISTER_MEM cmd = Family::cmdInitStoreRegisterMem;
    cmd.setRegisterAddress(offset);
    cmd.setMemoryAddress(address);
    remapOffset(&cmd);
    auto buffer = csr.getSpaceForCmd<MI_STORE_REGISTER_MEM>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSurfaceState<Family>::encodeBuffer(void *dst, uint64_t address, size_t size, uint32_t mocs,
                                              bool cpuCoherent, bool forceNonAuxMode, bool isReadOnly, uint32_t numAvailableDevices,
                                              GraphicsAllocation *allocation, GmmHelper *gmmHelper, bool useGlobalAtomics, size_t numDevicesInContext) {
    auto surfaceState = reinterpret_cast<R_SURFACE_STATE *>(dst);
    UNRECOVERABLE_IF(!isAligned<getSurfaceBaseAddressMinimumAlignment()>(size));

    SURFACE_STATE_BUFFER_LENGTH Length = {0};
    Length.Length = static_cast<uint32_t>(size - 1);

    surfaceState->setWidth(Length.SurfaceState.Width + 1);
    surfaceState->setHeight(Length.SurfaceState.Height + 1);
    surfaceState->setDepth(Length.SurfaceState.Depth + 1);

    surfaceState->setSurfaceType((address != 0) ? R_SURFACE_STATE::SURFACE_TYPE_SURFTYPE_BUFFER
                                                : R_SURFACE_STATE::SURFACE_TYPE_SURFTYPE_NULL);
    surfaceState->setSurfaceFormat(SURFACE_FORMAT::SURFACE_FORMAT_RAW);
    surfaceState->setSurfaceVerticalAlignment(R_SURFACE_STATE::SURFACE_VERTICAL_ALIGNMENT_VALIGN_4);
    surfaceState->setSurfaceHorizontalAlignment(R_SURFACE_STATE::SURFACE_HORIZONTAL_ALIGNMENT_HALIGN_4);

    surfaceState->setTileMode(R_SURFACE_STATE::TILE_MODE_LINEAR);
    surfaceState->setVerticalLineStride(0);
    surfaceState->setVerticalLineStrideOffset(0);
    surfaceState->setMemoryObjectControlState(mocs);
    surfaceState->setSurfaceBaseAddress(address);

    surfaceState->setCoherencyType(cpuCoherent ? R_SURFACE_STATE::COHERENCY_TYPE_IA_COHERENT
                                               : R_SURFACE_STATE::COHERENCY_TYPE_GPU_COHERENT);
    surfaceState->setAuxiliarySurfaceMode(AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_NONE);

    Gmm *gmm = allocation ? allocation->getDefaultGmm() : nullptr;
    if (gmm && gmm->isRenderCompressed && !forceNonAuxMode) {
        // Its expected to not program pitch/qpitch/baseAddress for Aux surface in CCS scenarios
        surfaceState->setCoherencyType(R_SURFACE_STATE::COHERENCY_TYPE_GPU_COHERENT);
        setBufferAuxParamsForCCS(surfaceState);
    }

    if (DebugManager.flags.DisableCachingForStatefulBufferAccess.get()) {
        surfaceState->setMemoryObjectControlState(gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CACHELINE_MISALIGNED));
    }

    EncodeSurfaceState<Family>::encodeExtraBufferParams(surfaceState, allocation, gmmHelper, isReadOnly, numAvailableDevices, useGlobalAtomics, numDevicesInContext);
}

template <typename Family>
void EncodeSurfaceState<Family>::getSshAlignedPointer(uintptr_t &ptr, size_t &offset) {
    auto sshAlignmentMask =
        getSurfaceBaseAddressAlignmentMask();
    uintptr_t alignedPtr = ptr & sshAlignmentMask;

    offset = 0;
    if (ptr != alignedPtr) {
        offset = ptrDiff(ptr, alignedPtr);
        ptr = alignedPtr;
    }
}

// Returned binding table pointer is relative to given heap (which is assumed to be the Surface state base addess)
// as required by the INTERFACE_DESCRIPTOR_DATA.
template <typename Family>
size_t EncodeSurfaceState<Family>::pushBindingTableAndSurfaceStates(IndirectHeap &dstHeap, size_t bindingTableCount,
                                                                    const void *srcKernelSsh, size_t srcKernelSshSize,
                                                                    size_t numberOfBindingTableStates, size_t offsetOfBindingTable) {
    using BINDING_TABLE_STATE = typename Family::BINDING_TABLE_STATE;
    using INTERFACE_DESCRIPTOR_DATA = typename Family::INTERFACE_DESCRIPTOR_DATA;
    using RENDER_SURFACE_STATE = typename Family::RENDER_SURFACE_STATE;

    if (bindingTableCount == 0) {
        // according to compiler, kernel does not reference BTIs to stateful surfaces, so there's nothing to patch
        return 0;
    }
    size_t sshSize = srcKernelSshSize;
    DEBUG_BREAK_IF(srcKernelSsh == nullptr);

    auto srcSurfaceState = srcKernelSsh;
    // Allocate space for new ssh data
    auto dstSurfaceState = dstHeap.getSpace(sshSize);

    // Compiler sends BTI table that is already populated with surface state pointers relative to local SSH.
    // We may need to patch these pointers so that they are relative to surface state base address
    if (dstSurfaceState == dstHeap.getCpuBase()) {
        // nothing to patch, we're at the start of heap (which is assumed to be the surface state base address)
        // we need to simply copy the ssh (including BTIs from compiler)
        memcpy_s(dstSurfaceState, sshSize, srcSurfaceState, sshSize);
        return offsetOfBindingTable;
    }

    // We can copy-over the surface states, but BTIs will need to be patched
    memcpy_s(dstSurfaceState, sshSize, srcSurfaceState, offsetOfBindingTable);

    uint32_t surfaceStatesOffset = static_cast<uint32_t>(ptrDiff(dstSurfaceState, dstHeap.getCpuBase()));

    // march over BTIs and offset the pointers based on surface state base address
    auto *dstBtiTableBase = reinterpret_cast<BINDING_TABLE_STATE *>(ptrOffset(dstSurfaceState, offsetOfBindingTable));
    DEBUG_BREAK_IF(reinterpret_cast<uintptr_t>(dstBtiTableBase) % INTERFACE_DESCRIPTOR_DATA::BINDINGTABLEPOINTER_ALIGN_SIZE != 0);
    auto *srcBtiTableBase = reinterpret_cast<const BINDING_TABLE_STATE *>(ptrOffset(srcSurfaceState, offsetOfBindingTable));
    BINDING_TABLE_STATE bti = Family::cmdInitBindingTableState;
    for (uint32_t i = 0, e = static_cast<uint32_t>(numberOfBindingTableStates); i != e; ++i) {
        uint32_t localSurfaceStateOffset = srcBtiTableBase[i].getSurfaceStatePointer();
        uint32_t offsetedSurfaceStateOffset = localSurfaceStateOffset + surfaceStatesOffset;
        bti.setSurfaceStatePointer(offsetedSurfaceStateOffset); // patch just the SurfaceStatePointer bits
        dstBtiTableBase[i] = bti;
        DEBUG_BREAK_IF(bti.getRawData(0) % sizeof(BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE) != 0);
    }

    return ptrDiff(dstBtiTableBase, dstHeap.getCpuBase());
}

template <typename Family>
void EncodeSurfaceState<Family>::encodeExtraCacheSettings(R_SURFACE_STATE *surfaceState, const HardwareInfo &hwInfo) {}

template <typename Family>
void EncodeSurfaceState<Family>::setImageAuxParamsForCCS(R_SURFACE_STATE *surfaceState, Gmm *gmm) {
    using AUXILIARY_SURFACE_MODE = typename Family::RENDER_SURFACE_STATE::AUXILIARY_SURFACE_MODE;
    // Its expected to not program pitch/qpitch/baseAddress for Aux surface in CCS scenarios
    surfaceState->setAuxiliarySurfaceMode(AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_CCS_E);
    setFlagsForMediaCompression(surfaceState, gmm);

    setClearColorParams(surfaceState, gmm);
    setUnifiedAuxBaseAddress<Family>(surfaceState, gmm);
}

template <typename Family>
void EncodeSurfaceState<Family>::setBufferAuxParamsForCCS(R_SURFACE_STATE *surfaceState) {
    using AUXILIARY_SURFACE_MODE = typename R_SURFACE_STATE::AUXILIARY_SURFACE_MODE;

    surfaceState->setAuxiliarySurfaceMode(AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_CCS_E);
}

template <typename Family>
bool EncodeSurfaceState<Family>::isAuxModeEnabled(R_SURFACE_STATE *surfaceState, Gmm *gmm) {
    using AUXILIARY_SURFACE_MODE = typename R_SURFACE_STATE::AUXILIARY_SURFACE_MODE;

    return (surfaceState->getAuxiliarySurfaceMode() == AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_CCS_E);
}

template <typename Family>
void *EncodeDispatchKernel<Family>::getInterfaceDescriptor(CommandContainer &container, uint32_t &iddOffset) {

    if (container.nextIddInBlock == container.getNumIddPerBlock()) {
        if (ApiSpecificConfig::getBindlessConfiguration()) {
            container.getDevice()->getBindlessHeapsHelper()->getHeap(BindlessHeapsHelper::BindlesHeapType::GLOBAL_DSH)->align(EncodeStates<Family>::alignInterfaceDescriptorData);
            container.setIddBlock(container.getDevice()->getBindlessHeapsHelper()->getSpaceInHeap(sizeof(INTERFACE_DESCRIPTOR_DATA) * container.getNumIddPerBlock(), BindlessHeapsHelper::BindlesHeapType::GLOBAL_DSH));
        } else {
            container.getIndirectHeap(HeapType::DYNAMIC_STATE)->align(EncodeStates<Family>::alignInterfaceDescriptorData);
            container.setIddBlock(container.getHeapSpaceAllowGrow(HeapType::DYNAMIC_STATE,
                                                                  sizeof(INTERFACE_DESCRIPTOR_DATA) * container.getNumIddPerBlock()));
        }
        container.nextIddInBlock = 0;

        EncodeMediaInterfaceDescriptorLoad<Family>::encode(container);
    }

    iddOffset = container.nextIddInBlock;
    auto interfaceDescriptorData = static_cast<INTERFACE_DESCRIPTOR_DATA *>(container.getIddBlock());
    return &interfaceDescriptorData[container.nextIddInBlock++];
}

template <typename Family>
bool EncodeDispatchKernel<Family>::inlineDataProgrammingRequired(const KernelDescriptor &kernelDesc) {
    auto checkKernelForInlineData = true;
    if (DebugManager.flags.EnablePassInlineData.get() != -1) {
        checkKernelForInlineData = !!DebugManager.flags.EnablePassInlineData.get();
    }
    if (checkKernelForInlineData) {
        return kernelDesc.kernelAttributes.flags.passInlineData;
    }
    return false;
}

template <typename GfxFamily>
void EncodeDispatchKernel<GfxFamily>::adjustTimestampPacket(WALKER_TYPE &walkerCmd, const HardwareInfo &hwInfo) {}

template <typename Family>
void EncodeIndirectParams<Family>::setGroupCountIndirect(CommandContainer &container, const NEO::CrossThreadDataOffset offsets[3], void *crossThreadAddress) {
    for (int i = 0; i < 3; ++i) {
        if (NEO::isUndefinedOffset(offsets[i])) {
            continue;
        }
        EncodeStoreMMIO<Family>::encode(*container.getCommandStream(), GPUGPU_DISPATCHDIM[i], ptrOffset(reinterpret_cast<uint64_t>(crossThreadAddress), offsets[i]));
    }
}

template <typename Family>
void EncodeDispatchKernel<Family>::adjustBindingTablePrefetch(INTERFACE_DESCRIPTOR_DATA &interfaceDescriptor, uint32_t samplerCount, uint32_t bindingTableEntryCount) {
    auto enablePrefetch = EncodeSurfaceState<Family>::doBindingTablePrefetch();
    if (DebugManager.flags.ForceBtpPrefetchMode.get() != -1) {
        enablePrefetch = static_cast<bool>(DebugManager.flags.ForceBtpPrefetchMode.get());
    }

    if (enablePrefetch) {
        interfaceDescriptor.setSamplerCount(static_cast<typename INTERFACE_DESCRIPTOR_DATA::SAMPLER_COUNT>((samplerCount + 3) / 4));
        interfaceDescriptor.setBindingTableEntryCount(std::min(bindingTableEntryCount, 31u));
    } else {
        interfaceDescriptor.setSamplerCount(INTERFACE_DESCRIPTOR_DATA::SAMPLER_COUNT::SAMPLER_COUNT_NO_SAMPLERS_USED);
        interfaceDescriptor.setBindingTableEntryCount(0u);
    }
}

template <typename Family>
void EncodeDispatchKernel<Family>::adjustInterfaceDescriptorData(INTERFACE_DESCRIPTOR_DATA &interfaceDescriptor, const HardwareInfo &hwInfo) {}

template <typename Family>
void EncodeIndirectParams<Family>::setGlobalWorkSizeIndirect(CommandContainer &container, const NEO::CrossThreadDataOffset offsets[3], void *crossThreadAddress, const uint32_t *lws) {
    for (int i = 0; i < 3; ++i) {
        if (NEO::isUndefinedOffset(offsets[i])) {
            continue;
        }
        EncodeMathMMIO<Family>::encodeMulRegVal(container, GPUGPU_DISPATCHDIM[i], lws[i], ptrOffset(reinterpret_cast<uint64_t>(crossThreadAddress), offsets[i]));
    }
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForIndirectParams() {
    return 3 * sizeof(typename Family::MI_LOAD_REGISTER_MEM);
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForSetGroupCountIndirect() {
    return 3 * (sizeof(MI_STORE_REGISTER_MEM));
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForSetGroupSizeIndirect() {
    return 3 * (sizeof(MI_LOAD_REGISTER_REG) + sizeof(MI_LOAD_REGISTER_IMM) + sizeof(MI_MATH) + sizeof(MI_MATH_ALU_INST_INLINE) + sizeof(MI_STORE_REGISTER_MEM));
}

template <typename Family>
void EncodeSempahore<Family>::addMiSemaphoreWaitCommand(LinearStream &commandStream,
                                                        uint64_t compareAddress,
                                                        uint32_t compareData,
                                                        COMPARE_OPERATION compareMode) {
    addMiSemaphoreWaitCommand(commandStream, compareAddress, compareData, compareMode, false);
}

template <typename Family>
void EncodeSempahore<Family>::addMiSemaphoreWaitCommand(LinearStream &commandStream,
                                                        uint64_t compareAddress,
                                                        uint32_t compareData,
                                                        COMPARE_OPERATION compareMode,
                                                        bool registerPollMode) {
    auto semaphoreCommand = commandStream.getSpaceForCmd<MI_SEMAPHORE_WAIT>();
    programMiSemaphoreWait(semaphoreCommand,
                           compareAddress,
                           compareData,
                           compareMode,
                           registerPollMode);
}

template <typename Family>
size_t EncodeSempahore<Family>::getSizeMiSemaphoreWait() {
    return sizeof(MI_SEMAPHORE_WAIT);
}

template <typename Family>
void EncodeAtomic<Family>::programMiAtomic(MI_ATOMIC *atomic,
                                           uint64_t writeAddress,
                                           ATOMIC_OPCODES opcode,
                                           DATA_SIZE dataSize,
                                           uint32_t returnDataControl,
                                           uint32_t csStall) {
    MI_ATOMIC cmd = Family::cmdInitAtomic;
    cmd.setAtomicOpcode(opcode);
    cmd.setDataSize(dataSize);
    cmd.setMemoryAddress(static_cast<uint32_t>(writeAddress & 0x0000FFFFFFFFULL));
    cmd.setMemoryAddressHigh(static_cast<uint32_t>(writeAddress >> 32));
    cmd.setReturnDataControl(returnDataControl);
    cmd.setCsStall(csStall);

    *atomic = cmd;
}

template <typename Family>
void EncodeAtomic<Family>::programMiAtomic(LinearStream &commandStream,
                                           uint64_t writeAddress,
                                           ATOMIC_OPCODES opcode,
                                           DATA_SIZE dataSize,
                                           uint32_t returnDataControl,
                                           uint32_t csStall) {
    auto miAtomic = commandStream.getSpaceForCmd<MI_ATOMIC>();
    EncodeAtomic<Family>::programMiAtomic(miAtomic, writeAddress, opcode, dataSize, returnDataControl, csStall);
}

template <typename Family>
void EncodeBatchBufferStartOrEnd<Family>::programBatchBufferStart(LinearStream *commandStream,
                                                                  uint64_t address,
                                                                  bool secondLevel) {
    MI_BATCH_BUFFER_START cmd = Family::cmdInitBatchBufferStart;
    if (secondLevel) {
        cmd.setSecondLevelBatchBuffer(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH);
    }
    cmd.setAddressSpaceIndicator(MI_BATCH_BUFFER_START::ADDRESS_SPACE_INDICATOR_PPGTT);
    cmd.setBatchBufferStartAddressGraphicsaddress472(address);
    auto buffer = commandStream->getSpaceForCmd<MI_BATCH_BUFFER_START>();
    *buffer = cmd;
}

template <typename Family>
void EncodeBatchBufferStartOrEnd<Family>::programBatchBufferEnd(CommandContainer &container) {
    MI_BATCH_BUFFER_END cmd = Family::cmdInitBatchBufferEnd;
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_BATCH_BUFFER_END>();
    *buffer = cmd;
}

template <typename GfxFamily>
void EncodeMiFlushDW<GfxFamily>::programMiFlushDw(LinearStream &commandStream, uint64_t immediateDataGpuAddress, uint64_t immediateData, bool timeStampOperation, bool commandWithPostSync) {
    programMiFlushDwWA(commandStream);

    auto miFlushDwCmd = commandStream.getSpaceForCmd<MI_FLUSH_DW>();
    MI_FLUSH_DW miFlush = GfxFamily::cmdInitMiFlushDw;
    if (commandWithPostSync) {
        auto postSyncType = timeStampOperation ? MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_TIMESTAMP_REGISTER : MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA_QWORD;
        miFlush.setPostSyncOperation(postSyncType);
        miFlush.setDestinationAddress(immediateDataGpuAddress);
        miFlush.setImmediateData(immediateData);
    }
    appendMiFlushDw(&miFlush);
    *miFlushDwCmd = miFlush;
}

template <typename GfxFamily>
size_t EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite() {
    return sizeof(typename GfxFamily::MI_FLUSH_DW) + EncodeMiFlushDW<GfxFamily>::getMiFlushDwWaSize();
}

template <typename GfxFamily>
void EncodeMemoryPrefetch<GfxFamily>::programMemoryPrefetch(LinearStream &commandStream, const GraphicsAllocation &graphicsAllocation, uint32_t size, const HardwareInfo &hwInfo) {}

template <typename GfxFamily>
size_t EncodeMemoryPrefetch<GfxFamily>::getSizeForMemoryPrefetch() { return 0; }

template <typename Family>
void EncodeMiArbCheck<Family>::program(LinearStream &commandStream) {
    auto miArbCheckStream = commandStream.getSpaceForCmd<MI_ARB_CHECK>();
    *miArbCheckStream = Family::cmdInitArbCheck;
}

template <typename Family>
size_t EncodeMiArbCheck<Family>::getCommandSize() { return sizeof(MI_ARB_CHECK); }

} // namespace NEO
