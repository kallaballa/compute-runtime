/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/blit_commands_helper.h"
#include "shared/test/common/cmd_parse/gen_cmd_parse.h"
#include "shared/test/common/fixtures/device_fixture.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"

#include "test.h"

using namespace NEO;

struct BlitColorTests : public DeviceFixture, public testing::TestWithParam<size_t> {
    void SetUp() override {
        DeviceFixture::SetUp();
    }

    void TearDown() override {
        DeviceFixture::TearDown();
    }
};

template <typename FamilyType>
class GivenLinearStreamWhenCallDispatchBlitMemoryColorFillThenCorrectDepthIsProgrammed {
  public:
    using XY_COLOR_BLT = typename FamilyType::XY_COLOR_BLT;
    using COLOR_DEPTH = typename XY_COLOR_BLT::COLOR_DEPTH;
    GivenLinearStreamWhenCallDispatchBlitMemoryColorFillThenCorrectDepthIsProgrammed(Device *device) : device(device) {}
    void TestBodyImpl(size_t patternSize, COLOR_DEPTH expectedDepth) {
        uint32_t streamBuffer[100] = {};
        LinearStream stream(streamBuffer, sizeof(streamBuffer));
        MockGraphicsAllocation mockAllocation(0, GraphicsAllocation::AllocationType::INTERNAL_HOST_MEMORY,
                                              reinterpret_cast<void *>(0x1234), 0x1000, 0, sizeof(uint32_t),
                                              MemoryPool::System4KBPages);
        uint32_t patternToCommand[4];
        memset(patternToCommand, 4, patternSize);
        BlitCommandsHelper<FamilyType>::dispatchBlitMemoryColorFill(&mockAllocation, patternToCommand, patternSize, stream, mockAllocation.getUnderlyingBufferSize(), *device->getExecutionEnvironment()->rootDeviceEnvironments[device->getRootDeviceIndex()]);
        GenCmdList cmdList;
        ASSERT_TRUE(FamilyType::PARSE::parseCommandBuffer(
            cmdList, ptrOffset(stream.getCpuBase(), 0), stream.getUsed()));
        auto itor = find<XY_COLOR_BLT *>(cmdList.begin(), cmdList.end());
        EXPECT_NE(cmdList.end(), itor);
        {
            auto cmd = genCmdCast<XY_COLOR_BLT *>(*itor);
            EXPECT_EQ(expectedDepth, cmd->getColorDepth());
        }
    }
    Device *device;
};
