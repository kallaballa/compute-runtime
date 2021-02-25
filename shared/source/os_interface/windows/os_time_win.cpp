/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/windows/os_time_win.h"

#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/os_interface/hw_info_config.h"
#include "shared/source/os_interface/windows/os_interface.h"
#include "shared/source/os_interface/windows/wddm/wddm.h"

#include <memory>

#undef WIN32_NO_STATUS

namespace NEO {

bool runEscape(Wddm *wddm, TimeStampDataHeader &escapeInfo) {
    if (wddm) {
        D3DKMT_ESCAPE escapeCommand = {0};

        GTDIGetGpuCpuTimestampsIn in = {GTDI_FNC_GET_GPU_CPU_TIMESTAMPS};
        uint32_t outSize = sizeof(GTDIGetGpuCpuTimestampsOut);

        escapeInfo.m_Header.EscapeCode = GFX_ESCAPE_IGPA_INSTRUMENTATION_CONTROL;
        escapeInfo.m_Header.Size = outSize;
        escapeInfo.m_Data.m_In = in;

        escapeCommand.Flags.Value = 0;
        escapeCommand.hAdapter = (D3DKMT_HANDLE)0;
        escapeCommand.hContext = (D3DKMT_HANDLE)0;                // escape is not context specific
        escapeCommand.hDevice = (D3DKMT_HANDLE)wddm->getDevice(); // escape not device specific, passing only for instrumentation
        escapeCommand.pPrivateDriverData = &escapeInfo;
        escapeCommand.PrivateDriverDataSize = sizeof(escapeInfo);
        escapeCommand.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;

        auto status = wddm->escape(escapeCommand);

        if (status == STATUS_SUCCESS) {
            return true;
        }
    }

    return false;
}

bool OSTimeWin::getCpuGpuTime(TimeStampData *pGpuCpuTime) {
    bool retVal = false;

    pGpuCpuTime->CPUTimeinNS = 0;
    pGpuCpuTime->GPUTimeStamp = 0;

    TimeStampDataHeader escapeInfo = {0};

    if (runEscape(wddm, escapeInfo)) {
        auto productFamily = wddm->getRootDeviceEnvironment().getHardwareInfo()->platform.eProductFamily;
        auto *hwInfoConfig = HwInfoConfig::get(productFamily);
        hwInfoConfig->convertTimestampsFromOaToCsDomain(escapeInfo.m_Data.m_Out.gpuPerfTicks);
        double cpuNanoseconds = escapeInfo.m_Data.m_Out.cpuPerfTicks *
                                (1000000000.0 / escapeInfo.m_Data.m_Out.cpuPerfFreq);

        pGpuCpuTime->CPUTimeinNS = (unsigned long long)cpuNanoseconds;
        pGpuCpuTime->GPUTimeStamp = (unsigned long long)escapeInfo.m_Data.m_Out.gpuPerfTicks;
        retVal = true;
    }

    return retVal;
}

bool OSTimeWin::getCpuTime(uint64_t *timeStamp) {
    uint64_t time;
    this->QueryPerfomanceCounterFnc((LARGE_INTEGER *)&time);

    *timeStamp = static_cast<uint64_t>((static_cast<double>(time) * NSEC_PER_SEC / frequency.QuadPart));
    return true;
};

std::unique_ptr<OSTime> OSTime::create(OSInterface *osInterface) {
    return std::unique_ptr<OSTime>(new OSTimeWin(osInterface));
}

OSTimeWin::OSTimeWin(OSInterface *osInterface) {
    this->osInterface = osInterface;
    if (osInterface) {
        wddm = osInterface->get()->getWddm();
    }
    QueryPerformanceFrequency(&frequency);
}

double OSTimeWin::getHostTimerResolution() const {
    double retValue = 0;
    if (frequency.QuadPart) {
        retValue = 1e9 / frequency.QuadPart;
    }
    return retValue;
}

double OSTimeWin::getDynamicDeviceTimerResolution(HardwareInfo const &hwInfo) const {
    double retVal = 0u;
    if (wddm) {
        retVal = 1000000000.0 / static_cast<double>(wddm->getTimestampFrequency());
    }

    return retVal;
}

uint64_t OSTimeWin::getCpuRawTimestamp() {
    LARGE_INTEGER cpuRawTimestamp = {};
    this->QueryPerfomanceCounterFnc(&cpuRawTimestamp);
    return cpuRawTimestamp.QuadPart;
}
} // namespace NEO
