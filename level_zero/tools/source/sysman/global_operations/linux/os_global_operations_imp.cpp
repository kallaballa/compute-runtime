/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/tools/source/sysman/global_operations/linux/os_global_operations_imp.h"

#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/tools/source/sysman/global_operations/global_operations_imp.h"
#include "level_zero/tools/source/sysman/linux/fs_access.h"
#include "level_zero/tools/source/sysman/sysman_const.h"
#include <level_zero/zet_api.h>

#include <chrono>
#include <time.h>

namespace L0 {

const std::string LinuxGlobalOperationsImp::deviceDir("device");
const std::string LinuxGlobalOperationsImp::vendorFile("device/vendor");
const std::string LinuxGlobalOperationsImp::deviceFile("device/device");
const std::string LinuxGlobalOperationsImp::subsystemVendorFile("device/subsystem_vendor");
const std::string LinuxGlobalOperationsImp::driverFile("device/driver");
const std::string LinuxGlobalOperationsImp::functionLevelReset("device/reset");
const std::string LinuxGlobalOperationsImp::clientsDir("clients");
const std::string LinuxGlobalOperationsImp::srcVersionFile("/sys/module/i915/srcversion");
const std::string LinuxGlobalOperationsImp::agamaVersionFile("/sys/module/i915/agama_version");
const std::string LinuxGlobalOperationsImp::ueventWedgedFile("/var/lib/libze_intel_gpu/wedged_file");

// Map engine entries(numeric values) present in /sys/class/drm/card<n>/clients/<client_n>/busy,
// with engine enum defined in leve-zero spec
// Note that entries with int 2 and 3(represented by i915 as CLASS_VIDEO and CLASS_VIDEO_ENHANCE)
// are both mapped to MEDIA, as CLASS_VIDEO represents any media fixed-function hardware.
static const std::map<int, zes_engine_type_flags_t> engineMap = {
    {0, ZES_ENGINE_TYPE_FLAG_3D},
    {1, ZES_ENGINE_TYPE_FLAG_DMA},
    {2, ZES_ENGINE_TYPE_FLAG_MEDIA},
    {3, ZES_ENGINE_TYPE_FLAG_MEDIA},
    {4, ZES_ENGINE_TYPE_FLAG_COMPUTE}};

void LinuxGlobalOperationsImp::getSerialNumber(char (&serialNumber)[ZES_STRING_PROPERTY_SIZE]) {
    std::strncpy(serialNumber, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
}

Device *LinuxGlobalOperationsImp::getDevice() {
    return pDevice;
}

void LinuxGlobalOperationsImp::getBoardNumber(char (&boardNumber)[ZES_STRING_PROPERTY_SIZE]) {
    std::strncpy(boardNumber, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
}

void LinuxGlobalOperationsImp::getBrandName(char (&brandName)[ZES_STRING_PROPERTY_SIZE]) {
    std::string strVal;
    ze_result_t result = pSysfsAccess->read(subsystemVendorFile, strVal);
    if (ZE_RESULT_SUCCESS != result) {
        std::strncpy(brandName, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
        return;
    }
    if (strVal.compare(intelPciId) == 0) {
        std::strncpy(brandName, vendorIntel.c_str(), ZES_STRING_PROPERTY_SIZE);
    } else {
        std::strncpy(brandName, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
    }
}

void LinuxGlobalOperationsImp::getModelName(char (&modelName)[ZES_STRING_PROPERTY_SIZE]) {
    std::string strVal;
    ze_result_t result = pSysfsAccess->read(deviceFile, strVal);
    if (ZE_RESULT_SUCCESS != result) {
        std::strncpy(modelName, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
        return;
    }
    std::strncpy(modelName, strVal.c_str(), ZES_STRING_PROPERTY_SIZE);
}

void LinuxGlobalOperationsImp::getVendorName(char (&vendorName)[ZES_STRING_PROPERTY_SIZE]) {
    std::string strVal;
    ze_result_t result = pSysfsAccess->read(vendorFile, strVal);
    if (ZE_RESULT_SUCCESS != result) {
        std::strncpy(vendorName, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
        return;
    }
    if (strVal.compare(intelPciId) == 0) {
        std::strncpy(vendorName, vendorIntel.c_str(), ZES_STRING_PROPERTY_SIZE);
    } else {
        std::strncpy(vendorName, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
    }
}

void LinuxGlobalOperationsImp::getDriverVersion(char (&driverVersion)[ZES_STRING_PROPERTY_SIZE]) {
    std::string strVal;
    ze_result_t result = pFsAccess->read(agamaVersionFile, strVal);
    if (ZE_RESULT_SUCCESS == result) {
        std::strncpy(driverVersion, strVal.c_str(), ZES_STRING_PROPERTY_SIZE);
        return;
    } else if ((result != ZE_RESULT_ERROR_NOT_AVAILABLE) && (result != ZE_RESULT_SUCCESS)) {
        std::strncpy(driverVersion, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
        return;
    } else {
        result = pFsAccess->read(srcVersionFile, strVal);
        if (ZE_RESULT_SUCCESS != result) {
            std::strncpy(driverVersion, unknown.c_str(), ZES_STRING_PROPERTY_SIZE);
            return;
        }
        std::strncpy(driverVersion, strVal.c_str(), ZES_STRING_PROPERTY_SIZE);
    }
}

static void getPidFdsForOpenDevice(ProcfsAccess *pProcfsAccess, SysfsAccess *pSysfsAccess, const ::pid_t pid, std::vector<int> &deviceFds) {
    // Return a list of all the file descriptors of this process that point to this device
    std::vector<int> fds;
    deviceFds.clear();
    if (ZE_RESULT_SUCCESS != pProcfsAccess->getFileDescriptors(pid, fds)) {
        // Process exited. Not an error. Just ignore.
        return;
    }
    for (auto &&fd : fds) {
        std::string file;
        if (pProcfsAccess->getFileName(pid, fd, file) != ZE_RESULT_SUCCESS) {
            // Process closed this file. Not an error. Just ignore.
            continue;
        }
        if (pSysfsAccess->isMyDeviceFile(file)) {
            deviceFds.push_back(fd);
        }
    }
}

ze_result_t LinuxGlobalOperationsImp::reset(ze_bool_t force) {
    std::string resetPath;
    std::string resetName;
    ze_result_t result = ZE_RESULT_SUCCESS;

    pSysfsAccess->getRealPath(functionLevelReset, resetPath);
    // Must run as root. Verify permission to perform reset.
    result = pFsAccess->canWrite(resetPath);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }
    pSysfsAccess->getRealPath(deviceDir, resetName);
    resetName = pFsAccess->getBaseName(resetName);

    ::pid_t myPid = pProcfsAccess->myProcessId();
    std::vector<int> myPidFds;
    std::vector<::pid_t> processes;

    result = pProcfsAccess->listProcesses(processes);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }
    for (auto &&pid : processes) {
        std::vector<int> fds;
        getPidFdsForOpenDevice(pProcfsAccess, pSysfsAccess, pid, fds);
        if (pid == myPid) {
            // L0 is expected to have this file open.
            // Keep list of fds. Close before unbind.
            myPidFds = fds;
        } else if (!fds.empty()) {
            if (force) {
                pProcfsAccess->kill(pid);
            } else {
                // Device is in use by another process.
                // Don't reset while in use.
                return ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE;
            }
        }
    }

    pLinuxSysmanImp->getSysmanDeviceImp()->pEngineHandleContext->releaseEngines();
    static_cast<DeviceImp *>(getDevice())->releaseResources();
    for (auto &&fd : myPidFds) {
        // Close open filedescriptors to the device
        // before unbinding device.
        // From this point forward, there is no
        // graceful way to fail the reset call.
        // All future ze calls by this process for this
        // device will fail.
        ::close(fd);
    }

    // Unbind the device from the kernel driver.
    result = pSysfsAccess->unbindDevice(resetName);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }

    // If someone opened the device
    // after we check, kill them here.
    result = pProcfsAccess->listProcesses(processes);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }
    std::vector<::pid_t> deviceUsingPids;
    deviceUsingPids.clear();
    for (auto &&pid : processes) {
        std::vector<int> fds;
        getPidFdsForOpenDevice(pProcfsAccess, pSysfsAccess, pid, fds);
        if (!fds.empty()) {

            // Kill all processes that have the device open.
            pProcfsAccess->kill(pid);
            deviceUsingPids.push_back(pid);
        }
    }

    // Wait for all the processes to exit
    // If they don't all exit within resetTimeout
    // just fail reset.
    auto start = std::chrono::steady_clock::now();
    auto end = start;
    for (auto &&pid : deviceUsingPids) {
        while (pProcfsAccess->isAlive(pid)) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() > resetTimeout) {
                return ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE;
            }

            struct ::timespec timeout = {.tv_sec = 0, .tv_nsec = 1000};
            ::nanosleep(&timeout, NULL);
            end = std::chrono::steady_clock::now();
        }
    }

    // Reset the device.
    result = pFsAccess->write(resetPath, "1");
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }

    // Rebind the device to the kernel driver.
    result = pSysfsAccess->bindDevice(resetName);
    if (ZE_RESULT_SUCCESS != result) {
        return result;
    }

    return ZE_RESULT_SUCCESS;
}

// Processes in the form of clients are present in sysfs like this:
// # /sys/class/drm/card0/clients$ ls
// 4  5
// # /sys/class/drm/card0/clients/4$ ls
// busy  name  pid
// # /sys/class/drm/card0/clients/4/busy$ ls
// 0  1  2  3
//
// Number of processes(If one process opened drm device multiple times, then multiple entries will be
// present for same process in clients directory) will be the number of clients
// (For example from above example, processes dirs are 4,5)
// Thus total number of times drm connection opened with this device will be 2.
// process.pid = pid (from above example)
// process.engines -> For each client's busy dir, numbers 0,1,2,3 represent engines and they contain
// accumulated nanoseconds each client spent on engines.
// Thus we traverse each file in busy dir for non-zero time and if we find that file say 0,then we could say that
// this engine 0 is used by process.
ze_result_t LinuxGlobalOperationsImp::scanProcessesState(std::vector<zes_process_state_t> &pProcessList) {
    std::vector<std::string> clientIds;
    struct deviceMemStruct {
        uint64_t deviceMemorySize;
        uint64_t deviceSharedMemorySize;
    };
    struct engineMemoryPairType {
        int64_t engineTypeField;
        deviceMemStruct deviceMemStructField;
    };

    ze_result_t result = pSysfsAccess->scanDirEntries(clientsDir, clientIds);
    if (ZE_RESULT_SUCCESS != result) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }

    // Create a map with unique pid as key and engineType as value
    std::map<uint64_t, engineMemoryPairType> pidClientMap;
    for (const auto &clientId : clientIds) {
        // realClientPidPath will be something like: clients/<clientId>/pid
        std::string realClientPidPath = clientsDir + "/" + clientId + "/" + "pid";
        uint64_t pid;
        result = pSysfsAccess->read(realClientPidPath, pid);
        if (ZE_RESULT_SUCCESS != result) {
            std::string bPidString;
            result = pSysfsAccess->read(realClientPidPath, bPidString);
            if (result == ZE_RESULT_SUCCESS) {
                size_t start = bPidString.find("<");
                size_t end = bPidString.find(">");
                std::string bPid = bPidString.substr(start + 1, end - start - 1);
                pid = std::stoull(bPid, nullptr, 10);
            }
        }
        if (ZE_RESULT_SUCCESS != result) {
            if (ZE_RESULT_ERROR_NOT_AVAILABLE == result) {
                continue;
            } else {
                return result;
            }
        }

        // Traverse the clients/<clientId>/busy directory to get accelerator engines used by process
        std::vector<std::string> engineNums;
        std::string busyDirForEngines = clientsDir + "/" + clientId + "/" + "busy";
        result = pSysfsAccess->scanDirEntries(busyDirForEngines, engineNums);
        if (ZE_RESULT_SUCCESS != result) {
            if (ZE_RESULT_ERROR_NOT_AVAILABLE == result) {
                continue;
            } else {
                return result;
            }
        }
        int64_t engineType = 0;
        // Scan all engine files present in /sys/class/drm/card0/clients/<ClientId>/busy and check
        // whether that engine is used by process
        for (const auto &engineNum : engineNums) {
            uint64_t timeSpent = 0;
            std::string engine = busyDirForEngines + "/" + engineNum;
            result = pSysfsAccess->read(engine, timeSpent);
            if (ZE_RESULT_SUCCESS != result) {
                if (ZE_RESULT_ERROR_NOT_AVAILABLE == result) {
                    continue;
                } else {
                    return result;
                }
            }
            if (timeSpent > 0) {
                int i915EnginNumber = stoi(engineNum);
                auto i915MapToL0EngineType = engineMap.find(i915EnginNumber);
                zes_engine_type_flags_t val = ZES_ENGINE_TYPE_FLAG_OTHER;
                if (i915MapToL0EngineType != engineMap.end()) {
                    // Found a valid map
                    val = i915MapToL0EngineType->second;
                }
                // In this for loop we want to retrieve the overall engines used by process
                engineType = engineType | val;
            }
        }

        uint64_t memSize = 0;
        std::string realClientTotalMemoryPath = clientsDir + "/" + clientId + "/" + "total_device_memory_buffer_objects" + "/" + "created_bytes";
        result = pSysfsAccess->read(realClientTotalMemoryPath, memSize);
        if (ZE_RESULT_SUCCESS != result) {
            if (ZE_RESULT_ERROR_NOT_AVAILABLE == result) {
                continue;
            } else {
                return result;
            }
        }

        uint64_t sharedMemSize = 0;
        std::string realClientTotalSharedMemoryPath = clientsDir + "/" + clientId + "/" + "total_device_memory_buffer_objects" + "/" + "imported_bytes";
        result = pSysfsAccess->read(realClientTotalSharedMemoryPath, sharedMemSize);
        if (ZE_RESULT_SUCCESS != result) {
            if (ZE_RESULT_ERROR_NOT_AVAILABLE == result) {
                continue;
            } else {
                return result;
            }
        }
        deviceMemStruct totalDeviceMem = {memSize, sharedMemSize};
        engineMemoryPairType engineMemoryPair = {engineType, totalDeviceMem};
        auto ret = pidClientMap.insert(std::make_pair(pid, engineMemoryPair));
        if (ret.second == false) {
            // insertion failed as entry with same pid already exists in map
            // Now update the engineMemoryPairType field for the existing pid entry
            engineMemoryPairType updateEngineMemoryPair;
            auto pidEntryFromMap = pidClientMap.find(pid);
            auto existingEngineType = pidEntryFromMap->second.engineTypeField;
            auto existingdeviceMemorySize = pidEntryFromMap->second.deviceMemStructField.deviceMemorySize;
            auto existingdeviceSharedMemorySize = pidEntryFromMap->second.deviceMemStructField.deviceSharedMemorySize;
            updateEngineMemoryPair.engineTypeField = existingEngineType | engineMemoryPair.engineTypeField;
            updateEngineMemoryPair.deviceMemStructField.deviceMemorySize = existingdeviceMemorySize + engineMemoryPair.deviceMemStructField.deviceMemorySize;
            updateEngineMemoryPair.deviceMemStructField.deviceSharedMemorySize = existingdeviceSharedMemorySize + engineMemoryPair.deviceMemStructField.deviceSharedMemorySize;
            pidClientMap[pid] = updateEngineMemoryPair;
        }
    }

    // iterate through all elements of pidClientMap
    for (auto itr = pidClientMap.begin(); itr != pidClientMap.end(); ++itr) {
        zes_process_state_t process;
        process.processId = static_cast<uint32_t>(itr->first);
        process.memSize = itr->second.deviceMemStructField.deviceMemorySize;
        process.sharedSize = itr->second.deviceMemStructField.deviceSharedMemorySize;
        process.engines = static_cast<uint32_t>(itr->second.engineTypeField);
        pProcessList.push_back(process);
    }
    return result;
}
ze_result_t LinuxGlobalOperationsImp::deviceGetState(zes_device_state_t *pState) {
    uint32_t valWedged = 0;
    ze_result_t result = pFsAccess->read(ueventWedgedFile, valWedged);
    if (result != ZE_RESULT_SUCCESS) {
        if (result == ZE_RESULT_ERROR_NOT_AVAILABLE)
            result = ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
        return result;
    }
    pState->reset = 0;
    if (valWedged != 0) {
        pState->reset |= ZES_RESET_REASON_FLAG_WEDGED;
    }
    return result;
}
LinuxGlobalOperationsImp::LinuxGlobalOperationsImp(OsSysman *pOsSysman) {
    pLinuxSysmanImp = static_cast<LinuxSysmanImp *>(pOsSysman);

    pSysfsAccess = &pLinuxSysmanImp->getSysfsAccess();
    pProcfsAccess = &pLinuxSysmanImp->getProcfsAccess();
    pFsAccess = &pLinuxSysmanImp->getFsAccess();
    pDevice = pLinuxSysmanImp->getDeviceHandle();
}

OsGlobalOperations *OsGlobalOperations::create(OsSysman *pOsSysman) {
    LinuxGlobalOperationsImp *pLinuxGlobalOperationsImp = new LinuxGlobalOperationsImp(pOsSysman);
    return static_cast<OsGlobalOperations *>(pLinuxGlobalOperationsImp);
}

} // namespace L0
