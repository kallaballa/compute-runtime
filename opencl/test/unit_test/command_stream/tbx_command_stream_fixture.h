/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/tbx_command_stream_receiver_hw.h"

#include "opencl/test/unit_test/command_stream/command_stream_fixture.h"

#include <cstdint>

namespace NEO {

class CommandStreamReceiver;
class MockDevice;

class TbxCommandStreamFixture : public CommandStreamFixture {
  public:
    virtual void SetUp(MockDevice *pDevice);
    void TearDown(void) override;

    CommandStreamReceiver *pCommandStreamReceiver = nullptr;

    MemoryManager *memoryManager = nullptr;
};
} // namespace NEO
