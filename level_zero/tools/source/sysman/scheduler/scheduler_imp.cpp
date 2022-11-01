/*
 * Copyright (C) 2020-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "scheduler_imp.h"

#include "shared/source/helpers/debug_helpers.h"

#include "level_zero/tools/source/sysman/sysman_const.h"

namespace L0 {

ze_result_t SchedulerImp::setExclusiveMode(ze_bool_t *pNeedReload) {
    return pOsScheduler->setExclusiveMode(pNeedReload);
}

ze_result_t SchedulerImp::setComputeUnitDebugMode(ze_bool_t *pNeedReload) {
    return pOsScheduler->setComputeUnitDebugMode(pNeedReload);
}

ze_result_t SchedulerImp::getCurrentMode(zes_sched_mode_t *pMode) {
    return pOsScheduler->getCurrentMode(pMode);
}

ze_result_t SchedulerImp::getTimeoutModeProperties(ze_bool_t getDefaults, zes_sched_timeout_properties_t *pConfig) {
    return pOsScheduler->getTimeoutModeProperties(getDefaults, pConfig);
}

ze_result_t SchedulerImp::getTimesliceModeProperties(ze_bool_t getDefaults, zes_sched_timeslice_properties_t *pConfig) {
    return pOsScheduler->getTimesliceModeProperties(getDefaults, pConfig);
}

ze_result_t SchedulerImp::setTimeoutMode(zes_sched_timeout_properties_t *pProperties, ze_bool_t *pNeedReload) {
    return pOsScheduler->setTimeoutMode(pProperties, pNeedReload);
}

ze_result_t SchedulerImp::setTimesliceMode(zes_sched_timeslice_properties_t *pProperties, ze_bool_t *pNeedReload) {
    return pOsScheduler->setTimesliceMode(pProperties, pNeedReload);
}

ze_result_t SchedulerImp::schedulerGetProperties(zes_sched_properties_t *pProperties) {
    *pProperties = properties;
    return ZE_RESULT_SUCCESS;
}

void SchedulerImp::init() {
    pOsScheduler->getProperties(this->properties);
}

SchedulerImp::SchedulerImp(OsSysman *pOsSysman, zes_engine_type_flag_t engineType, std::vector<std::string> &listOfEngines, ze_device_handle_t deviceHandle) {
    ze_device_properties_t deviceProperties = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
    Device::fromHandle(deviceHandle)->getProperties(&deviceProperties);
    pOsScheduler = OsScheduler::create(pOsSysman, engineType, listOfEngines,
                                       deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE, deviceProperties.subdeviceId);
    UNRECOVERABLE_IF(nullptr == pOsScheduler);
    init();
};

SchedulerImp::~SchedulerImp() {
    if (nullptr != pOsScheduler) {
        delete pOsScheduler;
        pOsScheduler = nullptr;
    }
}

} // namespace L0
