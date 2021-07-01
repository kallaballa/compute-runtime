/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/aub_mem_dump/definitions/aub_services.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/constants.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/xe_hp_core/hw_cmds.h"

#include "engine_node.h"

namespace NEO {

const char *HwMapper<IGFX_XE_HP_SDV>::abbreviation = "xehp";

bool isSimulationXEHP(unsigned short deviceId) {
    return false;
};

const PLATFORM XEHP::platform = {
    IGFX_XE_HP_SDV,
    PCH_UNKNOWN,
    IGFX_XE_HP_CORE,
    IGFX_XE_HP_CORE,
    PLATFORM_NONE, // default init
    0,             // usDeviceID
    0,             // usRevId. 0 sets the stepping to A0
    0,             // usDeviceID_PCH
    0,             // usRevId_PCH
    GTTYPE_UNDEFINED};

const RuntimeCapabilityTable XEHP::capabilityTable{
    EngineDirectSubmissionInitVec{
        {aub_stream::ENGINE_RCS, {true, true, false, true}},
        {aub_stream::ENGINE_CCS, {true, true, false, true}}}, // directSubmissionEngines
    {0, 0, 0, false, false, false},                           // kmdNotifyProperties
    MemoryConstants::max48BitAddress,                         // gpuAddressSpace
    83.333,                                                   // defaultProfilingTimerResolution
    MemoryConstants::pageSize,                                // requiredPreemptionSurfaceSize
    &isSimulationXEHP,                                        // isSimulation
    PreemptionMode::ThreadGroup,                              // defaultPreemptionMode
    aub_stream::ENGINE_CCS,                                   // defaultEngineType
    0,                                                        // maxRenderFrequency
    30,                                                       // clVersionSupport
    CmdServicesMemTraceVersion::DeviceValues::XeHP_SDV,       // aubDeviceId
    0,                                                        // extraQuantityThreadsPerEU
    64,                                                       // slmSize
    sizeof(XEHP::GRF),                                        // grfSize
    36u,                                                      // timestampValidBits
    32u,                                                      // kernelTimestampValidBits
    false,                                                    // blitterOperationsSupported
    true,                                                     // ftrSupportsInteger64BitAtomics
    true,                                                     // ftrSupportsFP64
    true,                                                     // ftrSupports64BitMath
    true,                                                     // ftrSvm
    false,                                                    // ftrSupportsCoherency
    false,                                                    // ftrSupportsVmeAvcTextureSampler
    false,                                                    // ftrSupportsVmeAvcPreemption
    false,                                                    // ftrRenderCompressedBuffers
    false,                                                    // ftrRenderCompressedImages
    true,                                                     // ftr64KBpages
    true,                                                     // instrumentationEnabled
    true,                                                     // forceStatelessCompilationFor32Bit
    "core",                                                   // platformType
    "",                                                       // deviceName
    true,                                                     // sourceLevelDebuggerSupported
    false,                                                    // supportsVme
    true,                                                     // supportCacheFlushAfterWalker
    true,                                                     // supportsImages
    false,                                                    // supportsDeviceEnqueue
    false,                                                    // supportsPipes
    true,                                                     // supportsOcl21Features
    false,                                                    // supportsOnDemandPageFaults
    false,                                                    // supportsIndependentForwardProgress
    false,                                                    // hostPtrTrackingEnabled
    true,                                                     // levelZeroSupported
    false,                                                    // isIntegratedDevice
    true,                                                     // supportsMediaBlock
    true                                                      // fusedEuEnabled
};

WorkaroundTable XEHP::workaroundTable = {};
FeatureTable XEHP::featureTable = {};

void XEHP::setupFeatureAndWorkaroundTable(HardwareInfo *hwInfo) {
    FeatureTable *featureTable = &hwInfo->featureTable;
    WorkaroundTable *workaroundTable = &hwInfo->workaroundTable;

    featureTable->ftrL3IACoherency = true;
    featureTable->ftrFlatPhysCCS = true;
    featureTable->ftrPPGTT = true;
    featureTable->ftrSVM = true;
    featureTable->ftrIA32eGfxPTEs = true;
    featureTable->ftrStandardMipTailFormat = true;
    featureTable->ftrTranslationTable = true;
    featureTable->ftrUserModeTranslationTable = true;
    featureTable->ftrTileMappedResource = true;
    featureTable->ftrEnableGuC = true;
    featureTable->ftrFbc = true;
    featureTable->ftrFbc2AddressTranslation = true;
    featureTable->ftrFbcBlitterTracking = true;
    featureTable->ftrAstcHdr2D = true;
    featureTable->ftrAstcLdr2D = true;

    featureTable->ftr3dMidBatchPreempt = true;
    featureTable->ftrGpGpuMidBatchPreempt = true;
    featureTable->ftrGpGpuThreadGroupLevelPreempt = true;
    featureTable->ftrPerCtxtPreemptionGranularityControl = true;

    featureTable->ftrTileY = false;
    featureTable->ftrLocalMemory = true;
    featureTable->ftrLinearCCS = true;
    featureTable->ftrE2ECompression = true;
    featureTable->ftrCCSNode = true;
    featureTable->ftrCCSRing = true;
    featureTable->ftrMultiTileArch = true;
    featureTable->ftrCCSMultiInstance = true;

    workaroundTable->wa4kAlignUVOffsetNV12LinearSurface = true;
    workaroundTable->waEnablePreemptionGranularityControlByUMD = true;
};

const HardwareInfo XEHP_CONFIG::hwInfo = {
    &XEHP::platform,
    &XEHP::featureTable,
    &XEHP::workaroundTable,
    &XEHP_CONFIG::gtSystemInfo,
    XEHP::capabilityTable,
};
GT_SYSTEM_INFO XEHP_CONFIG::gtSystemInfo = {0};
void XEHP_CONFIG::setupHardwareInfo(HardwareInfo *hwInfo, bool setupFeatureTableAndWorkaroundTable) {
    XEHP_CONFIG::setupHardwareInfoMultiTile(hwInfo, setupFeatureTableAndWorkaroundTable, false);
}

void XEHP_CONFIG::setupHardwareInfoMultiTile(HardwareInfo *hwInfo, bool setupFeatureTableAndWorkaroundTable, bool setupMultiTile) {
    GT_SYSTEM_INFO *gtSysInfo = &hwInfo->gtSystemInfo;
    gtSysInfo->CsrSizeInMb = 8;
    gtSysInfo->IsL3HashModeEnabled = false;
    gtSysInfo->IsDynamicallyPopulated = false;

    if (setupFeatureTableAndWorkaroundTable) {
        XEHP::setupFeatureAndWorkaroundTable(hwInfo);
    }
};
#include "hw_info_config_xehp.inl"
} // namespace NEO
