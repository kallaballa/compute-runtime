/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/command_container/command_encoder.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"

#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/kernel/kernel.h"
#include "opencl/test/unit_test/fixtures/built_in_fixture.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include <memory>

using namespace NEO;

struct HardwareCommandsTest : ClDeviceFixture,
                              ContextFixture,
                              BuiltInFixture,
                              ::testing::Test {

    using BuiltInFixture::SetUp;
    using ContextFixture::SetUp;

    void SetUp() override;
    void TearDown() override;

    void addSpaceForSingleKernelArg();

    size_t sizeRequiredCS;
    size_t sizeRequiredISH;

    std::unique_ptr<MockKernelWithInternals> mockKernelWithInternal;
    Kernel::SimpleKernelArgInfo kernelArgInfo = {};
    std::vector<Kernel::SimpleKernelArgInfo> kernelArguments;

    template <typename GfxFamily>
    size_t pushBindingTableAndSurfaceStates(IndirectHeap &dstHeap, const Kernel &srcKernel) {
        return EncodeSurfaceState<GfxFamily>::pushBindingTableAndSurfaceStates(dstHeap, (srcKernel.getKernelInfo(rootDeviceIndex).patchInfo.bindingTableState != nullptr) ? srcKernel.getKernelInfo(rootDeviceIndex).patchInfo.bindingTableState->Count : 0,
                                                                               srcKernel.getSurfaceStateHeap(rootDeviceIndex), srcKernel.getSurfaceStateHeapSize(rootDeviceIndex),
                                                                               srcKernel.getNumberOfBindingTableStates(rootDeviceIndex), srcKernel.getBindingTableOffset(rootDeviceIndex));
    }
};
