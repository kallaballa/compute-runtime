/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gen8/hw_cmds.h"

#include "opencl/source/mem_obj/image.h"
#include "opencl/source/mem_obj/image.inl"

#include <map>

namespace NEO {

typedef BDWFamily Family;
static auto gfxCore = IGFX_GEN8_CORE;

template <>
void ImageHw<Family>::setMediaSurfaceRotation(void *) {}

template <>
void ImageHw<Family>::setSurfaceMemoryObjectControlStateIndexToMocsTable(void *, uint32_t) {}

#include "opencl/source/mem_obj/image_factory_init.inl"
} // namespace NEO
