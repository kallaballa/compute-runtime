/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "CL/cl.h"

namespace NEO {
class MockContext;

class ContextFixture {
  protected:
    virtual void SetUp(cl_uint numDevices, cl_device_id *pDeviceList);
    virtual void TearDown();

    MockContext *pContext = nullptr;
};
} // namespace NEO
