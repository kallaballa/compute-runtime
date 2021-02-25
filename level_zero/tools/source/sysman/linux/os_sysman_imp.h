/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/non_copyable_or_moveable.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_interface.h"

#include "level_zero/core/source/device/device.h"
#include "level_zero/tools/source/sysman/linux/firmware_util/firmware_util.h"
#include "level_zero/tools/source/sysman/linux/fs_access.h"
#include "level_zero/tools/source/sysman/linux/pmt/pmt.h"
#include "level_zero/tools/source/sysman/linux/pmu/pmu_imp.h"
#include "level_zero/tools/source/sysman/linux/xml_parser/xml_parser.h"
#include "level_zero/tools/source/sysman/sysman_imp.h"

#include <map>

namespace L0 {
class PmuInterface;

class LinuxSysmanImp : public OsSysman, NEO::NonCopyableOrMovableClass {
  public:
    LinuxSysmanImp(SysmanDeviceImp *pParentSysmanDeviceImp);
    ~LinuxSysmanImp() override;

    ze_result_t init() override;

    XmlParser *getXmlParser();
    PmuInterface *getPmuInterface();
    FirmwareUtil *getFwUtilInterface();
    FsAccess &getFsAccess();
    ProcfsAccess &getProcfsAccess();
    SysfsAccess &getSysfsAccess();
    NEO::Drm &getDrm();
    PlatformMonitoringTech *getPlatformMonitoringTechAccess(uint32_t subDeviceId);
    Device *getDeviceHandle();
    SysmanDeviceImp *getSysmanDeviceImp();
    std::string getPciRootPortDirectoryPath(std::string realPciPath);

  protected:
    XmlParser *pXmlParser = nullptr;
    FsAccess *pFsAccess = nullptr;
    ProcfsAccess *pProcfsAccess = nullptr;
    SysfsAccess *pSysfsAccess = nullptr;
    NEO::Drm *pDrm = nullptr;
    Device *pDevice = nullptr;
    PmuInterface *pPmuInterface = nullptr;
    FirmwareUtil *pFwUtilInterface = nullptr;
    std::map<uint32_t, L0::PlatformMonitoringTech *> mapOfSubDeviceIdToPmtObject;

  private:
    LinuxSysmanImp() = delete;
    SysmanDeviceImp *pParentSysmanDeviceImp = nullptr;
};

} // namespace L0
