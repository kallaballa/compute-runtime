/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/aux_translation.h"

#include "opencl/source/helpers/properties_helper.h"

#include "hw_cmds.h"

namespace NEO {

class Kernel;
struct HardwareInfo;

template <typename GfxFamily>
struct UnitTestHelper {
    static bool isL3ConfigProgrammable();

    static bool evaluateDshUsage(size_t sizeBeforeEnqueue, size_t sizeAfterEnqueue, Kernel *kernel, uint32_t rootDeviceIndex);

    static bool isPageTableManagerSupported(const HardwareInfo &hwInfo);

    static bool isTimestampPacketWriteSupported();

    static bool isExpectMemoryNotEqualSupported();

    static uint32_t getDefaultSshUsage();

    static uint32_t getAppropriateThreadArbitrationPolicy(uint32_t policy);

    static bool evaluateGshAddressForScratchSpace(uint64_t usedScratchGpuAddress, uint64_t retrievedGshAddress);

    static bool isPipeControlWArequired(const HardwareInfo &hwInfo);

    static bool isAdditionalSynchronizationRequired();

    static bool isAdditionalMiSemaphoreWaitRequired();

    static bool isAdditionalMiSemaphoreWait(const typename GfxFamily::MI_SEMAPHORE_WAIT &semaphoreWait);

    static uint64_t getMemoryAddress(const typename GfxFamily::MI_ATOMIC &atomic);

    static bool requiresTimestampPacketsInSystemMemory();

    static const bool tiledImagesSupported;

    static const uint32_t smallestTestableSimdSize;

    static const AuxTranslationMode requiredAuxTranslationMode;

    static const bool useFullRowForLocalIdsGeneration;

    static const bool additionalMiFlushDwRequired;
};

} // namespace NEO
