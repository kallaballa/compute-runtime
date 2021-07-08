/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/tools/source/sysman/linux/pmt/pmt.h"

#include "shared/source/debug_settings/debug_settings_manager.h"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

namespace L0 {
const std::string PlatformMonitoringTech::baseTelemSysFS("/sys/class/intel_pmt");
const std::string PlatformMonitoringTech::telem("telem");
uint32_t PlatformMonitoringTech::rootDeviceTelemNodeIndex = 0;

ze_result_t PlatformMonitoringTech::readValue(const std::string key, uint32_t &value) {
    if (mappedMemory == nullptr) {
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    auto offset = keyOffsetMap.find(key);
    if (offset == keyOffsetMap.end()) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }
    value = *reinterpret_cast<uint32_t *>(mappedMemory + offset->second);
    return ZE_RESULT_SUCCESS;
}

ze_result_t PlatformMonitoringTech::readValue(const std::string key, uint64_t &value) {
    if (mappedMemory == nullptr) {
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    auto offset = keyOffsetMap.find(key);
    if (offset == keyOffsetMap.end()) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }
    value = *reinterpret_cast<uint64_t *>(mappedMemory + offset->second);
    return ZE_RESULT_SUCCESS;
}

bool compareTelemNodes(std::string &telemNode1, std::string &telemNode2) {
    std::string telem = "telem";
    auto indexString1 = telemNode1.substr(telem.size(), telemNode1.size());
    auto indexForTelemNode1 = stoi(indexString1);
    auto indexString2 = telemNode2.substr(telem.size(), telemNode2.size());
    auto indexForTelemNode2 = stoi(indexString2);
    return indexForTelemNode1 < indexForTelemNode2;
}

// Check if Telemetry node(say /sys/class/intel_pmt/telem1) and rootPciPathOfGpuDevice share same PCI Root port
static bool isValidTelemNode(FsAccess *pFsAccess, const std::string &rootPciPathOfGpuDevice, const std::string sysfsTelemNode) {
    std::string realPathOfTelemNode;
    auto result = pFsAccess->getRealPath(sysfsTelemNode, realPathOfTelemNode);
    if (result != ZE_RESULT_SUCCESS) {
        return false;
    }

    // Example: If
    // rootPciPathOfGpuDevice = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0";
    // realPathOfTelemNode = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem1";
    // As rootPciPathOfGpuDevice is a substring og realPathOfTelemNode , hence both sysfs telemNode and GPU device share same PCI Root.
    // Hence this telem node entry is valid for GPU device.
    return (realPathOfTelemNode.compare(0, rootPciPathOfGpuDevice.size(), rootPciPathOfGpuDevice) == 0);
}

ze_result_t PlatformMonitoringTech::enumerateRootTelemIndex(FsAccess *pFsAccess, std::string &rootPciPathOfGpuDevice) {
    std::vector<std::string> listOfTelemNodes;
    auto result = pFsAccess->listDirectory(baseTelemSysFS, listOfTelemNodes);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }

    // listOfTelemNodes vector could contain non "telem" entries which are not interested to us.
    // Lets refactor listOfTelemNodes vector as below
    for (auto iterator = listOfTelemNodes.begin(); iterator != listOfTelemNodes.end(); iterator++) {
        if (iterator->compare(0, telem.size(), telem) != 0) {
            listOfTelemNodes.erase(iterator--); // Remove entry if its suffix is not "telem"
        }
    }

    // Exmaple: For below directory
    // # /sys/class/intel_pmt$ ls
    // telem1  telem2  telem3
    // Then listOfTelemNodes would contain telem1, telem2, telem3
    std::sort(listOfTelemNodes.begin(), listOfTelemNodes.end(), compareTelemNodes); // sort listOfTelemNodes, to arange telem nodes in ascending order
    for (const auto &telemNode : listOfTelemNodes) {
        if (isValidTelemNode(pFsAccess, rootPciPathOfGpuDevice, baseTelemSysFS + "/" + telemNode)) {
            auto indexString = telemNode.substr(telem.size(), telemNode.size());
            rootDeviceTelemNodeIndex = stoi(indexString); // if telemNode is telemN, then rootDeviceTelemNodeIndex = N
            return ZE_RESULT_SUCCESS;
        }
    }
    return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
}

ze_result_t PlatformMonitoringTech::init(FsAccess *pFsAccess, const std::string &rootPciPathOfGpuDevice) {
    std::string telemNode = telem + std::to_string(rootDeviceTelemNodeIndex);
    if (isSubdevice) {
        uint32_t telemNodeIndex = 0;
        // If rootDeviceTelemNode is telem1, then rootDeviceTelemNodeIndex = 1
        // And thus for subdevice0 --> telem node will be telem2,
        // for subdevice1 --> telem node will be telem3 etc
        telemNodeIndex = rootDeviceTelemNodeIndex + subdeviceId + 1;
        telemNode = telem + std::to_string(telemNodeIndex);
    }
    std::string baseTelemSysFSNode = baseTelemSysFS + "/" + telemNode;
    if (!isValidTelemNode(pFsAccess, rootPciPathOfGpuDevice, baseTelemSysFSNode)) {
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    std::string telemetryDeviceEntry = baseTelemSysFSNode + "/" + telem;
    if (!pFsAccess->fileExists(telemetryDeviceEntry)) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Telemetry support not available. No file %s\n", telemetryDeviceEntry.c_str());
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    std::string guid;
    std::string guidPath = baseTelemSysFSNode + std::string("/guid");
    ze_result_t result = pFsAccess->read(guidPath, guid);
    if (ZE_RESULT_SUCCESS != result) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Telemetry sysfs entry not available %s\n", guidPath.c_str());
        return result;
    }
    result = getKeyOffsetMap(guid, keyOffsetMap);
    if (ZE_RESULT_SUCCESS != result) {
        // We didnt have any entry for this guid in guidToKeyOffsetMap
        return result;
    }

    std::string sizePath = baseTelemSysFSNode + std::string("/size");
    result = pFsAccess->read(sizePath, size);
    if (ZE_RESULT_SUCCESS != result) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Telemetry sysfs entry not available %s\n", sizePath.c_str());
        return result;
    }

    std::string offsetPath = baseTelemSysFSNode + std::string("/offset");
    result = pFsAccess->read(offsetPath, baseOffset);
    if (ZE_RESULT_SUCCESS != result) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Telemetry sysfs entry not available %s\n", offsetPath.c_str());
        return result;
    }

    int fd = this->openFunction(static_cast<const char *>(telemetryDeviceEntry.c_str()), O_RDONLY);
    if (fd == -1) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Failure opening telemetry file %s : %s \n", telemetryDeviceEntry.c_str(), strerror(errno));
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    mappedMemory = static_cast<char *>(this->mmapFunction(nullptr, static_cast<size_t>(size), PROT_READ, MAP_SHARED, fd, 0));
    if (mappedMemory == MAP_FAILED) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Failure mapping telemetry file %s : %s \n", telemetryDeviceEntry.c_str(), strerror(errno));
        this->closeFunction(fd);
        return ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE;
    }

    if (this->closeFunction(fd) == -1) {
        NEO::printDebugString(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr,
                              "Failure closing telemetry file %s : %s \n", telemetryDeviceEntry.c_str(), strerror(errno));
        this->munmapFunction(mappedMemory, size);
        mappedMemory = nullptr;
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }

    mappedMemory += baseOffset;
    return ZE_RESULT_SUCCESS;
}

PlatformMonitoringTech::PlatformMonitoringTech(FsAccess *pFsAccess, ze_bool_t onSubdevice,
                                               uint32_t subdeviceId) : subdeviceId(subdeviceId), isSubdevice(onSubdevice) {
}

void PlatformMonitoringTech::doInitPmtObject(FsAccess *pFsAccess, uint32_t subdeviceId, PlatformMonitoringTech *pPmt,
                                             const std::string &rootPciPathOfGpuDevice,
                                             std::map<uint32_t, L0::PlatformMonitoringTech *> &mapOfSubDeviceIdToPmtObject) {
    if (pPmt->init(pFsAccess, rootPciPathOfGpuDevice) == ZE_RESULT_SUCCESS) {
        mapOfSubDeviceIdToPmtObject.emplace(subdeviceId, pPmt);
        return;
    }
    delete pPmt; // We are here as pPmt->init failed and thus this pPmt object is not useful. Let's delete that.
}

void PlatformMonitoringTech::create(const std::vector<ze_device_handle_t> &deviceHandles,
                                    FsAccess *pFsAccess, std::string &rootPciPathOfGpuDevice,
                                    std::map<uint32_t, L0::PlatformMonitoringTech *> &mapOfSubDeviceIdToPmtObject) {
    if (ZE_RESULT_SUCCESS == PlatformMonitoringTech::enumerateRootTelemIndex(pFsAccess, rootPciPathOfGpuDevice)) {
        for (const auto &deviceHandle : deviceHandles) {
            ze_device_properties_t deviceProperties = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
            Device::fromHandle(deviceHandle)->getProperties(&deviceProperties);
            auto pPmt = new PlatformMonitoringTech(pFsAccess, deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE,
                                                   deviceProperties.subdeviceId);
            UNRECOVERABLE_IF(nullptr == pPmt);
            PlatformMonitoringTech::doInitPmtObject(pFsAccess, deviceProperties.subdeviceId, pPmt,
                                                    rootPciPathOfGpuDevice, mapOfSubDeviceIdToPmtObject);
        }
    }
}

PlatformMonitoringTech::~PlatformMonitoringTech() {
    if (mappedMemory != nullptr) {
        this->munmapFunction(mappedMemory - baseOffset, size);
    }
}

} // namespace L0
