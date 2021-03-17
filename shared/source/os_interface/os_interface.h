/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace NEO {
class ExecutionEnvironment;
class HwDeviceId;

class OSInterface {

  public:
    class OSInterfaceImpl;
    OSInterface();
    virtual ~OSInterface();
    OSInterface(const OSInterface &) = delete;
    OSInterface &operator=(const OSInterface &) = delete;

    OSInterfaceImpl *get() const {
        return osInterfaceImpl;
    };

    MOCKABLE_VIRTUAL bool isDebugAttachAvailable() const;
    static bool osEnabled64kbPages;
    static bool osEnableLocalMemory;
    static bool are64kbPagesEnabled();
    static bool newResourceImplicitFlush;
    static bool gpuIdleImplicitFlush;
    uint32_t getDeviceHandle() const;
    void setGmmInputArgs(void *args);
    static std::vector<std::unique_ptr<HwDeviceId>> discoverDevices(ExecutionEnvironment &executionEnvironment);

  protected:
    OSInterfaceImpl *osInterfaceImpl = nullptr;
};
} // namespace NEO
