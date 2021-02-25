/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/os_context_linux.h"

#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/source/os_interface/os_context.h"

namespace NEO {

OsContext *OsContext::create(OSInterface *osInterface, uint32_t contextId, DeviceBitfield deviceBitfield,
                             aub_stream::EngineType engineType, PreemptionMode preemptionMode,
                             bool lowPriority, bool internalEngine, bool rootDevice) {
    if (osInterface) {
        return new OsContextLinux(*osInterface->get()->getDrm(), contextId, deviceBitfield, engineType, preemptionMode,
                                  lowPriority, internalEngine, rootDevice);
    }
    return new OsContext(contextId, deviceBitfield, engineType, preemptionMode,
                         lowPriority, internalEngine, rootDevice);
}

OsContextLinux::OsContextLinux(Drm &drm, uint32_t contextId, DeviceBitfield deviceBitfield,
                               aub_stream::EngineType engineType, PreemptionMode preemptionMode,
                               bool lowPriority, bool internalEngine, bool rootDevice)
    : OsContext(contextId, deviceBitfield, engineType, preemptionMode, lowPriority, internalEngine, rootDevice),
      drm(drm) {
    auto hwInfo = drm.getRootDeviceEnvironment().getHardwareInfo();
    auto defaultEngineType = getChosenEngineType(*hwInfo);

    if (engineType == defaultEngineType && !lowPriority && !internalEngine) {
        this->setDefaultContext(true);
    }

    bool submitDirect = false;
    this->isDirectSubmissionAvailable(*drm.getRootDeviceEnvironment().getHardwareInfo(), submitDirect);

    for (auto deviceIndex = 0u; deviceIndex < deviceBitfield.size(); deviceIndex++) {
        if (deviceBitfield.test(deviceIndex)) {
            auto drmVmId = drm.getVirtualMemoryAddressSpace(deviceIndex);
            auto drmContextId = drm.createDrmContext(drmVmId, this->isDirectSubmissionActive());
            if (drm.areNonPersistentContextsSupported()) {
                drm.setNonPersistentContext(drmContextId);
            }

            if (drm.getRootDeviceEnvironment().executionEnvironment.isDebuggingEnabled() && !internalEngine) {
                drm.setContextDebugFlag(drmContextId);
            }

            if (drm.isPreemptionSupported() && lowPriority) {
                drm.setLowPriorityContextParam(drmContextId);
            }
            this->engineFlag = drm.bindDrmContext(drmContextId, deviceIndex, engineType);
            this->drmContextIds.push_back(drmContextId);

            if (drm.isPerContextVMRequired()) {
                auto ret = drm.queryVmId(drmContextId, drmVmId);
                DEBUG_BREAK_IF(drmVmId == 0);
                DEBUG_BREAK_IF(ret != 0);
                UNUSED_VARIABLE(ret);
                this->drmVmIds.push_back(drmVmId);
            }
        }
    }
}

Drm &OsContextLinux::getDrm() const {
    return this->drm;
}

void OsContextLinux::waitForPagingFence() {
    for (auto drmIterator = 0u; drmIterator < this->deviceBitfield.size(); drmIterator++) {
        if (this->deviceBitfield.test(drmIterator)) {
            drm.waitForBind(drmIterator);
        }
    }
}

OsContextLinux::~OsContextLinux() {
    for (auto drmContextId : drmContextIds) {
        drm.destroyDrmContext(drmContextId);
    }
}
} // namespace NEO
