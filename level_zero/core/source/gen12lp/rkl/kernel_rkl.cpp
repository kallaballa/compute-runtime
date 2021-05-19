/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/kernel/kernel_hw.h"

namespace L0 {

static KernelPopulateFactory<IGFX_ROCKETLAKE, KernelHw<IGFX_GEN12LP_CORE>> populateRKL;

} // namespace L0