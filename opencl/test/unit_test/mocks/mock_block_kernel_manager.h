/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/source/program/block_kernel_manager.h"

namespace NEO {

class MockBlockKernelManager : public BlockKernelManager {
  public:
    MockBlockKernelManager() = default;
    using BlockKernelManager::blockKernelInfoArray;
    using BlockKernelManager::blockPrivateSurfaceArray;
};
} // namespace NEO
