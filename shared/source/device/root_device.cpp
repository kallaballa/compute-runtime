/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/root_device.h"

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/device/sub_device.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/bindless_heaps_helper.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {
extern CommandStreamReceiver *createCommandStream(ExecutionEnvironment &executionEnvironment,
                                                  uint32_t rootDeviceIndex,
                                                  const DeviceBitfield deviceBitfield);

RootDevice::RootDevice(ExecutionEnvironment *executionEnvironment, uint32_t rootDeviceIndex) : Device(executionEnvironment), rootDeviceIndex(rootDeviceIndex) {}

RootDevice::~RootDevice() {
    for (auto subdevice : subdevices) {
        if (subdevice) {
            delete subdevice;
        }
    }
}

uint32_t RootDevice::getNumSubDevices() const {
    return this->numSubDevices;
}

BindlessHeapsHelper *RootDevice::getBindlessHeapsHelper() const {
    return this->getRootDeviceEnvironment().getBindlessHeapsHelper();
}
uint32_t RootDevice::getRootDeviceIndex() const {
    return rootDeviceIndex;
}

uint32_t RootDevice::getNumAvailableDevices() const {
    if (subdevices.empty()) {
        return 1u;
    }
    return getNumSubDevices();
}

Device *RootDevice::getDeviceById(uint32_t deviceId) const {
    if (subdevices.empty()) {
        return const_cast<RootDevice *>(this);
    }
    UNRECOVERABLE_IF(deviceId >= subdevices.size());
    return subdevices[deviceId];
}

Device *RootDevice::getParentDevice() const {
    return nullptr;
}

SubDevice *RootDevice::createSubDevice(uint32_t subDeviceIndex) {
    return Device::create<SubDevice>(executionEnvironment, subDeviceIndex, *this);
}

bool RootDevice::createDeviceImpl() {
    auto deviceMask = executionEnvironment->rootDeviceEnvironments[this->rootDeviceIndex]->deviceAffinityMask;
    deviceBitfield = maxNBitValue(HwHelper::getSubDevicesCount(&getHardwareInfo()));
    deviceBitfield &= deviceMask;
    numSubDevices = static_cast<uint32_t>(deviceBitfield.count());
    if (numSubDevices == 1) {
        numSubDevices = 0;
    }
    UNRECOVERABLE_IF(!subdevices.empty());
    if (numSubDevices) {
        subdevices.resize(HwHelper::getSubDevicesCount(&getHardwareInfo()), nullptr);
        for (auto i = 0u; i < HwHelper::getSubDevicesCount(&getHardwareInfo()); i++) {
            if (!deviceBitfield.test(i)) {
                continue;
            }
            auto subDevice = createSubDevice(i);
            if (!subDevice) {
                return false;
            }
            subdevices[i] = subDevice;
        }
    }
    auto status = Device::createDeviceImpl();
    if (!status) {
        return status;
    }
    if (ApiSpecificConfig::getBindlessConfiguration()) {
        this->executionEnvironment->rootDeviceEnvironments[getRootDeviceIndex()]->createBindlessHeapsHelper(getMemoryManager(), getNumAvailableDevices() > 1, rootDeviceIndex);
    }
    return true;
}

DeviceBitfield RootDevice::getDeviceBitfield() const {
    return deviceBitfield;
}

bool RootDevice::createEngines() {
    if (getNumSubDevices() < 2) {
        return Device::createEngines();
    } else {
        this->engineGroups.resize(static_cast<uint32_t>(EngineGroupType::MaxEngineGroups));
        initializeRootCommandStreamReceiver();
    }
    return true;
}

void RootDevice::initializeRootCommandStreamReceiver() {
    std::unique_ptr<CommandStreamReceiver> rootCommandStreamReceiver(createCommandStream(*executionEnvironment, rootDeviceIndex, getDeviceBitfield()));

    auto &hwInfo = getHardwareInfo();
    auto defaultEngineType = getChosenEngineType(hwInfo);
    auto preemptionMode = PreemptionHelper::getDefaultPreemptionMode(hwInfo);

    auto osContext = getMemoryManager()->createAndRegisterOsContext(rootCommandStreamReceiver.get(), defaultEngineType,
                                                                    getDeviceBitfield(), preemptionMode, false, false, true);

    rootCommandStreamReceiver->setupContext(*osContext);
    rootCommandStreamReceiver->initializeTagAllocation();
    rootCommandStreamReceiver->createGlobalFenceAllocation();
    rootCommandStreamReceiver->createWorkPartitionAllocation(*this);
    commandStreamReceivers.push_back(std::move(rootCommandStreamReceiver));

    EngineControl engine{commandStreamReceivers.back().get(), osContext};
    engines.push_back(engine);
    addEngineToEngineGroup(engine);
}

} // namespace NEO
