/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/gen9/hw_cmds.h"
#include "shared/source/helpers/populate_factory.h"

#include "opencl/source/device_queue/device_queue_hw.h"
#include "opencl/source/device_queue/device_queue_hw_bdw_plus.inl"
#include "opencl/source/device_queue/device_queue_hw_profiling.inl"

namespace NEO {
typedef SKLFamily Family;
static auto gfxCore = IGFX_GEN9_CORE;

template <>
void populateFactoryTable<DeviceQueueHw<Family>>() {
    extern DeviceQueueCreateFunc deviceQueueFactory[IGFX_MAX_CORE];
    deviceQueueFactory[gfxCore] = DeviceQueueHw<Family>::create;
}

template <>
size_t DeviceQueueHw<Family>::getWaCommandsSize() {
    return sizeof(Family::MI_ARB_CHECK) +
           sizeof(Family::MI_ATOMIC) +
           sizeof(Family::MI_ARB_CHECK) +
           sizeof(Family::PIPE_CONTROL) +
           sizeof(Family::PIPE_CONTROL);
}

template <>
void DeviceQueueHw<Family>::addArbCheckCmdWa() {
    auto arbCheck = slbCS.getSpaceForCmd<Family::MI_ARB_CHECK>();
    *arbCheck = Family::cmdInitArbCheck;
}

template <>
void DeviceQueueHw<Family>::addMiAtomicCmdWa(uint64_t atomicOpPlaceholder) {
    EncodeAtomic<Family>::programMiAtomic(slbCS,
                                          atomicOpPlaceholder,
                                          Family::MI_ATOMIC::ATOMIC_OPCODES::ATOMIC_8B_INCREMENT,
                                          Family::MI_ATOMIC::DATA_SIZE::DATA_SIZE_QWORD,
                                          0x1u, 0x1u);
}

template <>
void DeviceQueueHw<Family>::addLriCmdWa(bool setArbCheck) {}

template <>
void DeviceQueueHw<Family>::addPipeControlCmdWa(bool isNoopCmd) {
    auto pipeControl = slbCS.getSpaceForCmd<Family::PIPE_CONTROL>();
    if (isNoopCmd)
        memset(pipeControl, 0x0, sizeof(Family::PIPE_CONTROL));
    else
        initPipeControl(pipeControl);
}

template class DeviceQueueHw<Family>;
} // namespace NEO
