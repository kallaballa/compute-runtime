/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "level_zero/core/source/fabric/fabric_device_interface.h"
#include "level_zero/tools/source/sysman/linux/nl_api/iaf_nl_api.h"
#include <level_zero/ze_api.h>

#include <vector>

namespace L0 {

class FabricSubDeviceIaf;

struct FabricPortConnection {
    IafPortId currentid;
    uint8_t neighbourPortNumber;
    uint64_t neighbourGuid;
    uint32_t bandwidthInBytesPerNanoSecond;
    bool isDuplex;
};

class FabricDeviceIaf : public FabricDeviceInterface {

  public:
    FabricDeviceIaf(Device *device);
    ~FabricDeviceIaf() override = default;
    ze_result_t enumerate() override;
    std::vector<std::unique_ptr<FabricSubDeviceIaf>> subDeviceIafs = {};

  protected:
    Device *device = nullptr;
};

class FabricSubDeviceIaf : public FabricDeviceInterface {

  public:
    FabricSubDeviceIaf(Device *device);
    ~FabricSubDeviceIaf() override = default;
    ze_result_t enumerate() override;
    void setIafNlApi(IafNlApi *iafNlApi);
    std::vector<FabricPortConnection> connections = {};

  protected:
    ze_result_t getConnection(IafPort &port, FabricPortConnection &connection);

    std::unique_ptr<IafNlApi> pIafNlApi = nullptr;
    Device *device = nullptr;
    uint64_t guid = 0ull;
};

} // namespace L0