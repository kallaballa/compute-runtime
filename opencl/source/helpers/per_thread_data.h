/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/local_id_gen.h"

#include "patch_shared.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace NEO {
class LinearStream;

struct PerThreadDataHelper {
    static inline uint32_t getLocalIdSizePerThread(
        uint32_t simd,
        uint32_t grfSize,
        uint32_t numChannels) {
        return getPerThreadSizeLocalIDs(simd, grfSize, numChannels);
    }

    static inline size_t getPerThreadDataSizeTotal(
        uint32_t simd,
        uint32_t grfSize,
        uint32_t numChannels,
        size_t localWorkSize) {
        return getThreadsPerWG(simd, localWorkSize) * getLocalIdSizePerThread(simd, grfSize, numChannels);
    }

    static size_t sendPerThreadData(
        LinearStream &indirectHeap,
        uint32_t simd,
        uint32_t grfSize,
        uint32_t numChannels,
        const std::array<uint16_t, 3> &localWorkSizes,
        const std::array<uint8_t, 3> &workgroupWalkOrder,
        bool hasKernelOnlyImages);

    static inline uint32_t getNumLocalIdChannels(const iOpenCL::SPatchThreadPayload &threadPayload) {
        return threadPayload.LocalIDXPresent +
               threadPayload.LocalIDYPresent +
               threadPayload.LocalIDZPresent;
    }

    static uint32_t getThreadPayloadSize(const iOpenCL::SPatchThreadPayload &threadPayload, uint32_t simd, uint32_t grfSize);
};
} // namespace NEO
