/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/built_ins/built_ins.h"
#include "shared/test/common/cmd_parse/hw_parse.h"

#include "opencl/source/command_queue/enqueue_kernel.h"
#include "opencl/source/command_queue/gpgpu_walker.h"
#include "opencl/source/device_queue/device_queue.h"
#include "opencl/source/device_queue/device_queue_hw.h"
#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/scheduler/scheduler_kernel.h"
#include "opencl/test/unit_test/fixtures/execution_model_fixture.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

typedef Test<ExecutionModelSchedulerTest> BdwSchedulerTest;

BDWTEST_F(BdwSchedulerTest, givenCallToDispatchSchedulerWhenPipeControlWithCSStallIsAddedThenDCFlushEnabledIsSet) {

    using PIPE_CONTROL = typename FamilyType::PIPE_CONTROL;

    if (pClDevice->areOcl21FeaturesSupported()) {
        DeviceQueueHw<FamilyType> *pDevQueueHw = castToObject<DeviceQueueHw<FamilyType>>(pDevQueue);
        SchedulerKernel &scheduler = context->getSchedulerKernel();

        size_t minRequiredSizeForSchedulerSSH = HardwareCommandsHelper<FamilyType>::getSshSizeForExecutionModel(*parentKernel, rootDeviceIndex);

        // Setup heaps in pCmdQ
        MultiDispatchInfo multiDispatchinfo(&scheduler);
        LinearStream &commandStream = getCommandStream<FamilyType, CL_COMMAND_NDRANGE_KERNEL>(*pCmdQ, CsrDependencies(),
                                                                                              false, false, false, multiDispatchinfo,
                                                                                              nullptr, 0);
        pCmdQ->getIndirectHeap(IndirectHeap::SURFACE_STATE, minRequiredSizeForSchedulerSSH);

        GpgpuWalkerHelper<FamilyType>::dispatchScheduler(
            commandStream,
            *pDevQueueHw,
            pDevice->getPreemptionMode(),
            scheduler,
            &pCmdQ->getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
            pDevQueueHw->getIndirectHeap(IndirectHeap::DYNAMIC_STATE),
            false);

        HardwareParse hwParser;
        hwParser.parseCommands<FamilyType>(commandStream, 0);
        hwParser.findHardwareCommands<FamilyType>();

        ASSERT_NE(hwParser.cmdList.end(), hwParser.itorWalker);

        GenCmdList pcList = hwParser.getCommandsList<PIPE_CONTROL>();

        EXPECT_NE(0u, pcList.size());

        for (GenCmdList::iterator it = pcList.begin(); it != pcList.end(); it++) {
            PIPE_CONTROL *pc = (PIPE_CONTROL *)*it;
            ASSERT_NE(nullptr, pc);
            if (pc->getCommandStreamerStallEnable()) {
                EXPECT_TRUE(pc->getDcFlushEnable());
            }
        }
    }
}
