/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gen12lp/hw_cmds.h"
#include "shared/source/image/image_surface_state.h"

namespace NEO {

using Family = TGLLPFamily;

template <>
void setFilterMode<Family>(Family::RENDER_SURFACE_STATE *surfaceState, const HardwareInfo *hwInfo){};

// clang-format off
#include "shared/source/image/image_skl_plus.inl"
// clang-format on
} // namespace NEO
