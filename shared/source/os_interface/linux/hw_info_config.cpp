/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/hw_info_config.h"

#include "shared/source/command_stream/preemption.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/constants.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/source/utilities/cpu_info.h"

#include "hw_cmds.h"

#include <cstring>

namespace NEO {

HwInfoConfig *hwInfoConfigFactory[IGFX_MAX_PRODUCT] = {};

uint32_t bitExact(uint32_t value, uint32_t highBit, uint32_t lowBit) {
    uint32_t bitVal = static_cast<uint32_t>((value >> lowBit) & maxNBitValue(highBit - lowBit + 1));
    return bitVal;
}

int configureCacheInfo(HardwareInfo *hwInfo) {
    GT_SYSTEM_INFO *gtSystemInfo = &hwInfo->gtSystemInfo;

    uint32_t type = 0;
    uint32_t subleaf = 0;
    uint32_t eax, ebx, ecx;
    uint32_t cachelevel, linesize, partitions, ways;
    uint64_t sets, size;

    const CpuInfo &cpuInfo = CpuInfo::getInstance();

    do {
        uint32_t cpuRegsInfo[4];

        cpuInfo.cpuidex(cpuRegsInfo, 4, subleaf);
        eax = cpuRegsInfo[0];
        ebx = cpuRegsInfo[1];
        ecx = cpuRegsInfo[2];

        type = bitExact(eax, 4, 0);
        if (type != 0) {
            cachelevel = bitExact(eax, 7, 5);
            linesize = bitExact(ebx, 11, 0) + 1;
            partitions = bitExact(ebx, 21, 12) + 1;
            ways = bitExact(ebx, 31, 22) + 1;
            sets = (uint64_t)ecx + 1;

            size = sets * ways * partitions * linesize / 1024;
            if (cachelevel == 3) {
                gtSystemInfo->LLCCacheSizeInKb = size;
            }
            subleaf++;
        }
    } while (type);

    return 0;
}

int HwInfoConfig::configureHwInfo(const HardwareInfo *inHwInfo, HardwareInfo *outHwInfo, OSInterface *osIface) {
    int ret = 0;
    Drm *drm = osIface->get()->getDrm();

    *outHwInfo = *inHwInfo;
    auto platform = &outHwInfo->platform;
    auto gtSystemInfo = &outHwInfo->gtSystemInfo;
    auto featureTable = &outHwInfo->featureTable;

    int val = 0;
    ret = drm->getDeviceID(val);
    if (ret != 0 || val == 0) {
        *outHwInfo = {};
        return (ret == 0) ? -1 : ret;
    }
    platform->usDeviceID = static_cast<unsigned short>(val);
    ret = drm->getDeviceRevID(val);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    platform->usRevId = static_cast<unsigned short>(val);

    int sliceCount;
    int subSliceCount;
    int euCount;

    bool status = drm->queryTopology(*outHwInfo, sliceCount, subSliceCount, euCount);

    if (!status) {
        PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "%s", "WARNING: Topology query failed!\n");

        sliceCount = gtSystemInfo->SliceCount;

        ret = drm->getEuTotal(euCount);
        if (ret != 0) {
            PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "%s", "FATAL: Cannot query EU total parameter!\n");
            *outHwInfo = {};
            return ret;
        }

        ret = drm->getSubsliceTotal(subSliceCount);
        if (ret != 0) {
            PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "%s", "FATAL: Cannot query subslice total parameter!\n");
            *outHwInfo = {};
            return ret;
        }
    }

    gtSystemInfo->SliceCount = static_cast<uint32_t>(sliceCount);
    gtSystemInfo->SubSliceCount = static_cast<uint32_t>(subSliceCount);
    gtSystemInfo->EUCount = static_cast<uint32_t>(euCount);
    gtSystemInfo->ThreadCount = this->threadsPerEu * gtSystemInfo->EUCount;

    uint64_t gttSizeQuery = 0;
    featureTable->ftrSVM = true;

    ret = drm->queryGttSize(gttSizeQuery);

    if (ret == 0) {
        featureTable->ftrSVM = (gttSizeQuery > MemoryConstants::max64BitAppAddress);
        outHwInfo->capabilityTable.gpuAddressSpace = gttSizeQuery - 1; // gttSizeQuery = (1 << bits)
    }

    int maxGpuFreq = 0;
    drm->getMaxGpuFrequency(*outHwInfo, maxGpuFreq);

    GTTYPE gtType = drm->getGtType();
    if (gtType == GTTYPE_UNDEFINED) {
        *outHwInfo = {};
        return -1;
    }
    platform->eGTType = gtType;
    featureTable->ftrGTA = (gtType == GTTYPE_GTA) ? 1 : 0;
    featureTable->ftrGTC = (gtType == GTTYPE_GTC) ? 1 : 0;
    featureTable->ftrGTX = (gtType == GTTYPE_GTX) ? 1 : 0;
    featureTable->ftrGT1 = (gtType == GTTYPE_GT1) ? 1 : 0;
    featureTable->ftrGT1_5 = (gtType == GTTYPE_GT1_5) ? 1 : 0;
    featureTable->ftrGT2 = (gtType == GTTYPE_GT2) ? 1 : 0;
    featureTable->ftrGT2_5 = (gtType == GTTYPE_GT2_5) ? 1 : 0;
    featureTable->ftrGT3 = (gtType == GTTYPE_GT3) ? 1 : 0;
    featureTable->ftrGT4 = (gtType == GTTYPE_GT4) ? 1 : 0;

    ret = configureHardwareCustom(outHwInfo, osIface);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    configureCacheInfo(outHwInfo);
    featureTable->ftrEDram = (gtSystemInfo->EdramSizeInKb != 0) ? 1 : 0;

    outHwInfo->capabilityTable.maxRenderFrequency = maxGpuFreq;
    outHwInfo->capabilityTable.ftrSvm = featureTable->ftrSVM;

    HwHelper &hwHelper = HwHelper::get(platform->eRenderCoreFamily);
    outHwInfo->capabilityTable.ftrSupportsCoherency = false;

    hwHelper.adjustDefaultEngineType(outHwInfo);
    outHwInfo->capabilityTable.defaultEngineType = getChosenEngineType(*outHwInfo);

    drm->checkQueueSliceSupport();
    drm->checkNonPersistentContextsSupport();
    drm->checkPreemptionSupport();
    bool preemption = drm->isPreemptionSupported();
    PreemptionHelper::adjustDefaultPreemptionMode(outHwInfo->capabilityTable,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuMidThreadLevelPreempt) && preemption,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuThreadGroupLevelPreempt) && preemption,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuMidBatchPreempt) && preemption);
    outHwInfo->capabilityTable.requiredPreemptionSurfaceSize = outHwInfo->gtSystemInfo.CsrSizeInMb * MemoryConstants::megaByte;

    auto &kmdNotifyProperties = outHwInfo->capabilityTable.kmdNotifyProperties;
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableKmdNotify.get(), kmdNotifyProperties.enableKmdNotify);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideKmdNotifyDelayMicroseconds.get(), kmdNotifyProperties.delayKmdNotifyMicroseconds);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableQuickKmdSleep.get(), kmdNotifyProperties.enableQuickKmdSleep);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideQuickKmdSleepDelayMicroseconds.get(), kmdNotifyProperties.delayQuickKmdSleepMicroseconds);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableQuickKmdSleepForSporadicWaits.get(), kmdNotifyProperties.enableQuickKmdSleepForSporadicWaits);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideDelayQuickKmdSleepForSporadicWaitsMicroseconds.get(), kmdNotifyProperties.delayQuickKmdSleepForSporadicWaitsMicroseconds);

    return 0;
}

} // namespace NEO
