/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <level_zero/zes_api.h>

#include <map>
#include <vector>

struct _zes_engine_handle_t {
    virtual ~_zes_engine_handle_t() = default;
};

namespace L0 {

struct OsSysman;

class Engine : _zes_engine_handle_t {
  public:
    virtual ze_result_t engineGetProperties(zes_engine_properties_t *pProperties) = 0;
    virtual ze_result_t engineGetActivity(zes_engine_stats_t *pStats) = 0;

    static Engine *fromHandle(zes_engine_handle_t handle) {
        return static_cast<Engine *>(handle);
    }
    inline zes_engine_handle_t toHandle() { return this; }
};

struct EngineHandleContext {
    EngineHandleContext(OsSysman *pOsSysman);
    ~EngineHandleContext();

    void init();
    void releaseEngines();

    ze_result_t engineGet(uint32_t *pCount, zes_engine_handle_t *phEngine);

    OsSysman *pOsSysman = nullptr;
    std::vector<Engine *> handleList = {};

  private:
    void createHandle(zes_engine_group_t engineType, uint32_t engineInstance);
};

} // namespace L0
