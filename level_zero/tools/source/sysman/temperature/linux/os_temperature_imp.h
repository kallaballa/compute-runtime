/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "level_zero/tools/source/sysman/temperature/os_temperature.h"

namespace L0 {

class SysfsAccess;
class PlatformMonitoringTech;
class LinuxTemperatureImp : public OsTemperature, NEO::NonCopyableOrMovableClass {
  public:
    ze_result_t getProperties(zes_temp_properties_t *pProperties) override;
    ze_result_t getSensorTemperature(double *pTemperature) override;
    bool isTempModuleSupported() override;
    void setSensorType(zes_temp_sensors_t sensorType);
    LinuxTemperatureImp(OsSysman *pOsSysman);
    LinuxTemperatureImp() = default;
    ~LinuxTemperatureImp() override = default;

  protected:
    PlatformMonitoringTech *pPmt = nullptr;
    zes_temp_sensors_t type = ZES_TEMP_SENSORS_GLOBAL;
};
} // namespace L0
