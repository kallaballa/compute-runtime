/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/device/root_device.h"
#include "shared/source/device/sub_device.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/test/common/helpers/variable_backup.h"

#include "opencl/test/unit_test/fixtures/mock_aub_center_fixture.h"

namespace NEO {
class CommandStreamReceiver;
class DriverInfo;
class OSTime;

template <typename GfxFamily>
class UltCommandStreamReceiver;

extern CommandStreamReceiver *createCommandStream(ExecutionEnvironment &executionEnvironment,
                                                  uint32_t rootDeviceIndex,
                                                  const DeviceBitfield deviceBitfield);

struct MockSubDevice : public SubDevice {
    using SubDevice::getDeviceBitfield;
    using SubDevice::getGlobalMemorySize;
    using SubDevice::SubDevice;

    std::unique_ptr<CommandStreamReceiver> createCommandStreamReceiver() const override {
        return std::unique_ptr<CommandStreamReceiver>(createCommandStreamReceiverFunc(*executionEnvironment, getRootDeviceIndex(), getDeviceBitfield()));
    }
    static decltype(&createCommandStream) createCommandStreamReceiverFunc;
};

class MockDevice : public RootDevice {
  public:
    using Device::commandStreamReceivers;
    using Device::createDeviceInternals;
    using Device::createEngine;
    using Device::deviceInfo;
    using Device::engineGroups;
    using Device::engines;
    using Device::executionEnvironment;
    using Device::getGlobalMemorySize;
    using Device::initializeCaps;
    using RootDevice::createEngines;
    using RootDevice::defaultEngineIndex;
    using RootDevice::getDeviceBitfield;
    using RootDevice::initializeRootCommandStreamReceiver;
    using RootDevice::numSubDevices;
    using RootDevice::subdevices;

    void setOSTime(OSTime *osTime);
    void setDriverInfo(DriverInfo *driverInfo);

    static bool createSingleDevice;
    bool createDeviceImpl() override;

    bool getCpuTime(uint64_t *timeStamp) { return true; };
    MockDevice();
    MockDevice(ExecutionEnvironment *executionEnvironment, uint32_t rootDeviceIndex);

    void setPreemptionMode(PreemptionMode mode) {
        preemptionMode = mode;
    }

    void injectMemoryManager(MemoryManager *);

    void setPerfCounters(PerformanceCounters *perfCounters) {
        if (perfCounters) {
            performanceCounters = std::unique_ptr<PerformanceCounters>(perfCounters);
        } else {
            performanceCounters.release();
        }
    }

    const char *getProductAbbrev() const;

    template <typename T>
    UltCommandStreamReceiver<T> &getUltCommandStreamReceiver() {
        return reinterpret_cast<UltCommandStreamReceiver<T> &>(*engines[defaultEngineIndex].commandStreamReceiver);
    }

    template <typename T>
    UltCommandStreamReceiver<T> &getUltCommandStreamReceiverFromIndex(uint32_t index) {
        return reinterpret_cast<UltCommandStreamReceiver<T> &>(*engines[index].commandStreamReceiver);
    }
    CommandStreamReceiver &getGpgpuCommandStreamReceiver() const { return *engines[defaultEngineIndex].commandStreamReceiver; }
    void resetCommandStreamReceiver(CommandStreamReceiver *newCsr);
    void resetCommandStreamReceiver(CommandStreamReceiver *newCsr, uint32_t engineIndex);

    void setDebuggerActive(bool active) {
        this->deviceInfo.debuggerActive = active;
    }

    template <typename T>
    static T *createWithExecutionEnvironment(const HardwareInfo *pHwInfo, ExecutionEnvironment *executionEnvironment, uint32_t rootDeviceIndex) {
        pHwInfo = pHwInfo ? pHwInfo : defaultHwInfo.get();
        executionEnvironment->rootDeviceEnvironments[rootDeviceIndex]->setHwInfo(pHwInfo);
        T *device = new T(executionEnvironment, rootDeviceIndex);
        return createDeviceInternals(device);
    }

    template <typename T>
    static T *createWithNewExecutionEnvironment(const HardwareInfo *pHwInfo, uint32_t rootDeviceIndex = 0) {
        ExecutionEnvironment *executionEnvironment = new ExecutionEnvironment();
        auto numRootDevices = DebugManager.flags.CreateMultipleRootDevices.get() ? DebugManager.flags.CreateMultipleRootDevices.get() : rootDeviceIndex + 1;
        executionEnvironment->prepareRootDeviceEnvironments(numRootDevices);
        pHwInfo = pHwInfo ? pHwInfo : defaultHwInfo.get();
        for (auto i = 0u; i < executionEnvironment->rootDeviceEnvironments.size(); i++) {
            executionEnvironment->rootDeviceEnvironments[i]->setHwInfo(pHwInfo);
        }
        return createWithExecutionEnvironment<T>(pHwInfo, executionEnvironment, rootDeviceIndex);
    }

    SubDevice *createSubDevice(uint32_t subDeviceIndex) override {
        return Device::create<MockSubDevice>(executionEnvironment, subDeviceIndex, *this);
    }

    std::unique_ptr<CommandStreamReceiver> createCommandStreamReceiver() const override {
        return std::unique_ptr<CommandStreamReceiver>(createCommandStreamReceiverFunc(*executionEnvironment, getRootDeviceIndex(), getDeviceBitfield()));
    }

    static decltype(&createCommandStream) createCommandStreamReceiverFunc;
};

template <>
inline Device *MockDevice::createWithNewExecutionEnvironment<Device>(const HardwareInfo *pHwInfo, uint32_t rootDeviceIndex) {
    auto executionEnvironment = new ExecutionEnvironment();
    executionEnvironment->prepareRootDeviceEnvironments(1);
    MockAubCenterFixture::setMockAubCenter(*executionEnvironment->rootDeviceEnvironments[0]);
    auto hwInfo = pHwInfo ? pHwInfo : defaultHwInfo.get();
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(hwInfo);
    executionEnvironment->initializeMemoryManager();
    return Device::create<RootDevice>(executionEnvironment, 0u);
}

class FailDevice : public MockDevice {
  public:
    FailDevice(ExecutionEnvironment *executionEnvironment, uint32_t deviceIndex);
};

class FailDeviceAfterOne : public MockDevice {
  public:
    FailDeviceAfterOne(ExecutionEnvironment *executionEnvironment, uint32_t deviceIndex);
};

class MockAlignedMallocManagerDevice : public MockDevice {
  public:
    MockAlignedMallocManagerDevice(ExecutionEnvironment *executionEnvironment, uint32_t deviceIndex);
};

struct EnvironmentWithCsrWrapper {
    template <typename CsrType>
    void setCsrType() {
        createSubDeviceCsrFuncBackup = EnvironmentWithCsrWrapper::createCommandStreamReceiver<CsrType>;
        createRootDeviceCsrFuncBackup = EnvironmentWithCsrWrapper::createCommandStreamReceiver<CsrType>;
    }

    template <typename CsrType>
    static CommandStreamReceiver *createCommandStreamReceiver(ExecutionEnvironment &executionEnvironment,
                                                              uint32_t rootDeviceIndex,
                                                              const DeviceBitfield deviceBitfield) {
        return new CsrType(executionEnvironment, rootDeviceIndex, deviceBitfield);
    }

    VariableBackup<decltype(MockSubDevice::createCommandStreamReceiverFunc)> createSubDeviceCsrFuncBackup{&MockSubDevice::createCommandStreamReceiverFunc};
    VariableBackup<decltype(MockDevice::createCommandStreamReceiverFunc)> createRootDeviceCsrFuncBackup{&MockDevice::createCommandStreamReceiverFunc};
};
} // namespace NEO
