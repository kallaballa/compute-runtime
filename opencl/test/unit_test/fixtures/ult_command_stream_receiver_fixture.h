/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/cache_policy.h"
#include "shared/source/memory_manager/graphics_allocation.h"
#include "shared/test/common/cmd_parse/hw_parse.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/helpers/unit_test_helper.h"
#include "opencl/test/unit_test/libult/ult_command_stream_receiver.h"

namespace NEO {

struct UltCommandStreamReceiverTest
    : public ClDeviceFixture,
      public HardwareParse,
      ::testing::Test {
    void SetUp() override {
        ClDeviceFixture::SetUp();
        HardwareParse::SetUp();

        size_t sizeStream = 512;
        size_t alignmentStream = 0x1000;
        cmdBuffer = alignedMalloc(sizeStream, alignmentStream);
        dshBuffer = alignedMalloc(sizeStream, alignmentStream);
        iohBuffer = alignedMalloc(sizeStream, alignmentStream);
        sshBuffer = alignedMalloc(sizeStream, alignmentStream);

        ASSERT_NE(nullptr, cmdBuffer);
        ASSERT_NE(nullptr, dshBuffer);
        ASSERT_NE(nullptr, iohBuffer);
        ASSERT_NE(nullptr, sshBuffer);

        commandStream.replaceBuffer(cmdBuffer, sizeStream);
        auto graphicsAllocation = new MockGraphicsAllocation(cmdBuffer, sizeStream);
        commandStream.replaceGraphicsAllocation(graphicsAllocation);

        dsh.replaceBuffer(dshBuffer, sizeStream);
        graphicsAllocation = new MockGraphicsAllocation(dshBuffer, sizeStream);
        dsh.replaceGraphicsAllocation(graphicsAllocation);

        ioh.replaceBuffer(iohBuffer, sizeStream);

        graphicsAllocation = new MockGraphicsAllocation(iohBuffer, sizeStream);
        ioh.replaceGraphicsAllocation(graphicsAllocation);

        ssh.replaceBuffer(sshBuffer, sizeStream);
        graphicsAllocation = new MockGraphicsAllocation(sshBuffer, sizeStream);
        ssh.replaceGraphicsAllocation(graphicsAllocation);

        pDevice->getGpgpuCommandStreamReceiver().setupContext(*pDevice->getDefaultEngine().osContext);
    }

    void TearDown() override {
        pDevice->getGpgpuCommandStreamReceiver().flushBatchedSubmissions();
        delete dsh.getGraphicsAllocation();
        delete ioh.getGraphicsAllocation();
        delete ssh.getGraphicsAllocation();
        delete commandStream.getGraphicsAllocation();

        alignedFree(sshBuffer);
        alignedFree(iohBuffer);
        alignedFree(dshBuffer);
        alignedFree(cmdBuffer);
        HardwareParse::TearDown();
        ClDeviceFixture::TearDown();
    }

    template <typename CommandStreamReceiverType>
    CompletionStamp flushTask(CommandStreamReceiverType &commandStreamReceiver,
                              bool block = false,
                              size_t startOffset = 0,
                              bool requiresCoherency = false,
                              bool lowPriority = false) {

        flushTaskFlags.blocking = block;
        flushTaskFlags.requiresCoherency = requiresCoherency;
        flushTaskFlags.lowPriority = lowPriority;
        flushTaskFlags.preemptionMode = PreemptionHelper::getDefaultPreemptionMode(pDevice->getHardwareInfo());

        return commandStreamReceiver.flushTask(
            commandStream,
            startOffset,
            dsh,
            ioh,
            ssh,
            taskLevel,
            flushTaskFlags,
            *pDevice);
    }

    template <typename GfxFamily>
    void configureCSRHeapStatesToNonDirty() {
        auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<GfxFamily>();
        commandStreamReceiver.dshState.updateAndCheck(&dsh);
        commandStreamReceiver.iohState.updateAndCheck(&ioh);
        commandStreamReceiver.sshState.updateAndCheck(&ssh);
    }

    template <typename GfxFamily>
    void configureCSRtoNonDirtyState(bool isL1CacheEnabled) {
        bool slmUsed = false;
        if (DebugManager.flags.ForceSLML3Config.get()) {
            slmUsed = true;
        }

        uint32_t L3Config = PreambleHelper<GfxFamily>::getL3Config(*defaultHwInfo, slmUsed);

        auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<GfxFamily>();
        commandStreamReceiver.isPreambleSent = true;
        commandStreamReceiver.isEnginePrologueSent = true;
        commandStreamReceiver.lastPreemptionMode = pDevice->getPreemptionMode();
        commandStreamReceiver.setMediaVFEStateDirty(false);
        auto gmmHelper = pDevice->getGmmHelper();
        auto mocsIndex = isL1CacheEnabled ? gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CONST) : gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER);

        commandStreamReceiver.latestSentStatelessMocsConfig = mocsIndex >> 1;
        commandStreamReceiver.lastSentL3Config = L3Config;
        configureCSRHeapStatesToNonDirty<GfxFamily>();
        commandStreamReceiver.taskLevel = taskLevel;

        commandStreamReceiver.lastSentThreadArbitrationPolicy = commandStreamReceiver.requiredThreadArbitrationPolicy;
        commandStreamReceiver.lastSentCoherencyRequest = 0;
        commandStreamReceiver.lastMediaSamplerConfig = 0;
    }

    template <typename GfxFamily>
    UltCommandStreamReceiver<GfxFamily> &getUltCommandStreamReceiver() {
        return reinterpret_cast<UltCommandStreamReceiver<GfxFamily> &>(pDevice->getGpgpuCommandStreamReceiver());
    }

    DispatchFlags flushTaskFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
    uint32_t taskLevel = 42;
    LinearStream commandStream;
    IndirectHeap dsh = {nullptr};
    IndirectHeap ioh = {nullptr};
    IndirectHeap ssh = {nullptr};

    void *cmdBuffer = nullptr;
    void *dshBuffer = nullptr;
    void *iohBuffer = nullptr;
    void *sshBuffer = nullptr;

    uint32_t latestSentDcFlushTaskCount;
    uint32_t latestSentNonDcFlushTaskCount;
    uint32_t dcFlushRequiredTaskCount;
};
} // namespace NEO
