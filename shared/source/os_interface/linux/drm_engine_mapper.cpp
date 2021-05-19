/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_engine_mapper.h"

#include "shared/source/helpers/debug_helpers.h"

#include "drm/i915_drm.h"

namespace NEO {

unsigned int DrmEngineMapper::engineNodeMap(aub_stream::EngineType engineType) {
    if (aub_stream::ENGINE_RCS == engineType) {
        return I915_EXEC_RENDER;
    } else if (aub_stream::ENGINE_BCS == engineType) {
        return I915_EXEC_BLT;
    }
    UNRECOVERABLE_IF(true);
}
} // namespace NEO
