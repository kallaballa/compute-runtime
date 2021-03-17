/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "level_zero/core/test/unit_tests/mock.h"
#include "level_zero/tools/source/sysman/global_operations/global_operations_imp.h"
#include "level_zero/tools/source/sysman/global_operations/linux/os_global_operations_imp.h"

namespace L0 {
namespace ult {

const std::string vendorIntel("Intel(R) Corporation");
const std::string unknown("Unknown");
const std::string intelPciId("0x8086");
const std::string deviceDir("device");
const std::string vendorFile("device/vendor");
const std::string deviceFile("device/device");
const std::string subsystemVendorFile("device/subsystem_vendor");
const std::string driverFile("device/driver");
const std::string agamaVersionFile("/sys/module/i915/agama_version");
const std::string srcVersionFile("/sys/module/i915/srcversion");
const std::string functionLevelReset("device/reset");
const std::string clientsDir("clients");
constexpr uint64_t pid1 = 1711u;
constexpr uint64_t pid2 = 1722u;
constexpr uint64_t pid3 = 1723u;
constexpr uint64_t pid4 = 1733u;
const std::string bPid4 = "<1733>";
constexpr uint64_t engineTimeSpent = 123456u;
const std::string clientId1("4");
const std::string clientId2("5");
const std::string clientId3("6");
const std::string clientId4("7");
const std::string clientId5("8");
const std::string clientId6("10");
const std::string engine0("0");
const std::string engine1("1");
const std::string engine2("2");
const std::string engine3("3");
std::string driverVersion("5.0.0-37-generic SMP mod_unload");
std::string srcVersion("5.0.0-37");
const std::string ueventWedgedFile("/var/lib/libze_intel_gpu/wedged_file");
const std::string mockFunctionResetPath("/MOCK_FUNCTION_LEVEL_RESET_PATH");
const std::string mockDeviceDir("/MOCK_DEVICE_DIR");
const std::string mockDeviceName("/MOCK_DEVICE_NAME");

class GlobalOperationsSysfsAccess : public SysfsAccess {};

template <>
struct Mock<GlobalOperationsSysfsAccess> : public GlobalOperationsSysfsAccess {
    ze_result_t getRealPathVal(const std::string file, std::string &val) {
        if (file.compare(functionLevelReset) == 0) {
            val = mockFunctionResetPath;
        } else if (file.compare(deviceDir) == 0) {
            val = mockDeviceDir;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValString(const std::string file, std::string &val) {
        if (file.compare(subsystemVendorFile) == 0) {
            val = "0x8086";
        } else if (file.compare(deviceFile) == 0) {
            val = "0x3ea5";
        } else if (file.compare(vendorFile) == 0) {
            val = "0x8086";
        } else if (file.compare("clients/8/pid") == 0) {
            val = bPid4;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getFalseValString(const std::string file, std::string &val) {
        if (file.compare(subsystemVendorFile) == 0) {
            val = "0xa086";
        } else if (file.compare(deviceFile) == 0) {
            val = "0xa123";
        } else if (file.compare(vendorFile) == 0) {
            val = "0xa086";
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }
    ze_result_t getValUnsignedLong(const std::string file, uint64_t &val) {
        if ((file.compare("clients/4/pid") == 0) || (file.compare("clients/5/pid") == 0)) {
            val = pid1;
        } else if (file.compare("clients/6/pid") == 0) {
            val = pid2;
        } else if (file.compare("clients/7/pid") == 0) {
            val = pid3;
        } else if ((file.compare("clients/4/busy/0") == 0) || (file.compare("clients/4/busy/3") == 0) ||
                   (file.compare("clients/5/busy/1") == 0) || (file.compare("clients/6/busy/0") == 0) ||
                   (file.compare("clients/8/busy/1") == 0) || (file.compare("clients/8/busy/0") == 0)) {
            val = engineTimeSpent;
        } else if ((file.compare("clients/4/busy/1") == 0) || (file.compare("clients/4/busy/2") == 0) ||
                   (file.compare("clients/5/busy/0") == 0) || (file.compare("clients/5/busy/2") == 0) ||
                   (file.compare("clients/7/busy/0") == 0) || (file.compare("clients/7/busy/2") == 0) ||
                   (file.compare("clients/5/busy/3") == 0) || (file.compare("clients/6/busy/1") == 0) ||
                   (file.compare("clients/6/busy/2") == 0) || (file.compare("clients/6/busy/3") == 0) ||
                   (file.compare("clients/8/busy/2") == 0) || (file.compare("clients/8/busy/3") == 0)) {
            val = 0;
        } else if ((file.compare("clients/4/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/5/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/6/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/8/total_device_memory_buffer_objects/created_bytes") == 0)) {
            val = 1024;
        } else if ((file.compare("clients/4/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/5/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/6/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/8/total_device_memory_buffer_objects/imported_bytes") == 0)) {
            val = 512;
        } else if (file.compare("clients/7/total_device_memory_buffer_objects/created_bytes") == 0) {
            return ZE_RESULT_ERROR_UNKNOWN;
        } else if (file.compare("clients/7/total_device_memory_buffer_objects/imported_bytes") == 0) {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValUnsignedLongCreatedBytesSuccess(const std::string file, uint64_t &val) {
        if ((file.compare("clients/4/pid") == 0) || (file.compare("clients/5/pid") == 0)) {
            val = pid1;
        } else if (file.compare("clients/6/pid") == 0) {
            val = pid2;
        } else if (file.compare("clients/7/pid") == 0) {
            val = pid3;
        } else if ((file.compare("clients/4/busy/0") == 0) || (file.compare("clients/4/busy/3") == 0) ||
                   (file.compare("clients/5/busy/1") == 0) || (file.compare("clients/6/busy/0") == 0) ||
                   (file.compare("clients/8/busy/1") == 0) || (file.compare("clients/8/busy/0") == 0)) {
            val = engineTimeSpent;
        } else if ((file.compare("clients/4/busy/1") == 0) || (file.compare("clients/4/busy/2") == 0) ||
                   (file.compare("clients/5/busy/0") == 0) || (file.compare("clients/5/busy/2") == 0) ||
                   (file.compare("clients/7/busy/0") == 0) || (file.compare("clients/7/busy/2") == 0) ||
                   (file.compare("clients/5/busy/3") == 0) || (file.compare("clients/6/busy/1") == 0) ||
                   (file.compare("clients/6/busy/2") == 0) || (file.compare("clients/6/busy/3") == 0) ||
                   (file.compare("clients/8/busy/2") == 0) || (file.compare("clients/8/busy/3") == 0)) {
            val = 0;
        } else if ((file.compare("clients/4/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/5/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/6/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/8/total_device_memory_buffer_objects/created_bytes") == 0) ||
                   (file.compare("clients/7/total_device_memory_buffer_objects/created_bytes") == 0)) {
            val = 1024;
        } else if ((file.compare("clients/4/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/5/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/6/total_device_memory_buffer_objects/imported_bytes") == 0) ||
                   (file.compare("clients/8/total_device_memory_buffer_objects/imported_bytes") == 0)) {
            val = 512;
        } else if (file.compare("clients/7/total_device_memory_buffer_objects/imported_bytes") == 0) {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getScannedDir4Entries(const std::string path, std::vector<std::string> &list) {
        if (path.compare(clientsDir) == 0) {
            list.push_back(clientId1);
            list.push_back(clientId2);
            list.push_back(clientId3);
            list.push_back(clientId4);
            list.push_back(clientId5);
            list.push_back(clientId6);
        } else if ((path.compare("clients/4/busy") == 0) || (path.compare("clients/5/busy") == 0) ||
                   (path.compare("clients/6/busy") == 0) || (path.compare("clients/7/busy") == 0) ||
                   (path.compare("clients/8/busy") == 0)) {
            list.push_back(engine0);
            list.push_back(engine1);
            list.push_back(engine2);
            list.push_back(engine3);
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getScannedDirEntries(const std::string path, std::vector<std::string> &list) {
        if (path.compare(clientsDir) == 0) {
            list.push_back(clientId1);
            list.push_back(clientId2);
            list.push_back(clientId3);
            list.push_back(clientId5);
        } else if ((path.compare("clients/4/busy") == 0) || (path.compare("clients/5/busy") == 0) ||
                   (path.compare("clients/6/busy") == 0) || (path.compare("clients/8/busy") == 0)) {
            list.push_back(engine0);
            list.push_back(engine1);
            list.push_back(engine2);
            list.push_back(engine3);
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t mockDeviceOpSuccess(const std::string device) {
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t mockDeviceOpFailure(const std::string device) {
        return ZE_RESULT_ERROR_UNKNOWN;
    }

    bool mockIsMyDeviceFile(const std::string dev) {
        if (dev.compare(mockDeviceName) == 0) {
            return true;
        }
        return false;
    }

    Mock<GlobalOperationsSysfsAccess>() = default;

    MOCK_METHOD(ze_result_t, read, (const std::string file, std::string &val), (override));
    MOCK_METHOD(ze_result_t, read, (const std::string file, uint64_t &val), (override));
    MOCK_METHOD(ze_result_t, scanDirEntries, (const std::string path, std::vector<std::string> &list), (override));
    MOCK_METHOD(ze_result_t, getRealPath, (const std::string path, std::string &val), (override));
    MOCK_METHOD(ze_result_t, bindDevice, (const std::string device), (override));
    MOCK_METHOD(ze_result_t, unbindDevice, (const std::string device), (override));
    MOCK_METHOD(bool, fileExists, (const std::string file), (override));
    MOCK_METHOD(bool, isMyDeviceFile, (const std::string dev), (override));
};

class GlobalOperationsProcfsAccess : public ProcfsAccess {};

template <>
struct Mock<GlobalOperationsProcfsAccess> : public GlobalOperationsProcfsAccess {

    const ::pid_t extraPid = 4;
    const int extraFd = 5;
    std::vector<::pid_t> pidList = {1, 2, 3};
    std::vector<int> fdList = {0, 1, 2};
    ::pid_t ourDevicePid = 0;
    int ourDeviceFd = 0;

    ze_result_t mockProcessListDeviceUnused(std::vector<::pid_t> &list) {
        list = pidList;
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t mockProcessListDeviceInUse(std::vector<::pid_t> &list) {
        list = pidList;
        if (ourDevicePid) {
            list.push_back(ourDevicePid);
        }
        return ZE_RESULT_SUCCESS;
    }

    ::pid_t getMockMyProcessId() {
        return ::getpid();
    }

    ze_result_t getMockFileDescriptors(const ::pid_t pid, std::vector<int> &list) {
        // Give every process 3 file descriptors
        // Except the device that MOCK has the device open. Give it one extra.
        list.clear();
        list = fdList;
        if (ourDevicePid == pid) {
            list.push_back(ourDeviceFd);
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getMockFileName(const ::pid_t pid, const int fd, std::string &val) {
        if (pid == ourDevicePid && fd == ourDeviceFd) {
            val = mockDeviceName;
        } else {
            // return fake filenames for other file descriptors
            val = std::string("/FILENAME") + std::to_string(fd);
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getMockFileNameReturnError(const ::pid_t pid, const int fd, std::string &val) {
        return ZE_RESULT_ERROR_UNKNOWN;
    }

    bool mockIsAlive(const ::pid_t pid) {
        if (pid == ourDevicePid) {
            return true;
        }
        return false;
    }

    void mockKill(const ::pid_t pid) {
        ourDevicePid = 0;
    }

    Mock<GlobalOperationsProcfsAccess>() = default;

    MOCK_METHOD(ze_result_t, listProcesses, (std::vector<::pid_t> & list), (override));
    MOCK_METHOD(::pid_t, myProcessId, (), (override));
    MOCK_METHOD(ze_result_t, getFileDescriptors, (const ::pid_t pid, std::vector<int> &list), (override));
    MOCK_METHOD(ze_result_t, getFileName, (const ::pid_t pid, const int fd, std::string &val), (override));
    MOCK_METHOD(bool, isAlive, (const ::pid_t pid), (override));
    MOCK_METHOD(void, kill, (const ::pid_t pid), (override));
};

class GlobalOperationsFsAccess : public FsAccess {};

template <>
struct Mock<GlobalOperationsFsAccess> : public GlobalOperationsFsAccess {
    ze_result_t getValAgamaFile(const std::string file, std::string &val) {
        if (file.compare(agamaVersionFile) == 0) {
            val = driverVersion;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }

        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValSrcFile(const std::string file, std::string &val) {
        if (file.compare(srcVersionFile) == 0) {
            val = srcVersion;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValWedgedFileTrue(const std::string file, uint32_t &val) {
        if (file.compare(ueventWedgedFile) == 0) {
            val = 1;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValWedgedFileFalse(const std::string file, uint32_t &val) {
        if (file.compare(ueventWedgedFile) == 0) {
            val = 0;
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t getValWedgedFileNotFound(const std::string file, uint32_t &val) {
        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t getValWedgedFileInsufficientPermissions(const std::string file, uint32_t &val) {
        return ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS;
    }

    ze_result_t getValSrcFileNotAvaliable(const std::string file, std::string &val) {
        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t getValNotAvaliable(const std::string file, std::string &val) {
        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t getValErrorUnkown(const std::string file, std::string &val) {
        return ZE_RESULT_ERROR_UNKNOWN;
    }

    ze_result_t getValPermissionDenied(const std::string file, std::string &val) {
        return ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS;
    }

    ze_result_t getPermissionDenied(const std::string file) {
        return ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS;
    }

    ze_result_t getSuccess(const std::string file) {
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t writeSuccess(const std::string file, const std::string val) {
        return ZE_RESULT_SUCCESS;
    }

    Mock<GlobalOperationsFsAccess>() = default;

    MOCK_METHOD(ze_result_t, read, (const std::string file, std::string &val), (override));
    MOCK_METHOD(ze_result_t, read, (const std::string file, uint32_t &val), (override));
    MOCK_METHOD(ze_result_t, write, (const std::string file, const std::string val), (override));
    MOCK_METHOD(ze_result_t, canWrite, (const std::string file), (override));
};

class PublicLinuxGlobalOperationsImp : public L0::LinuxGlobalOperationsImp {
  public:
    using LinuxGlobalOperationsImp::resetTimeout;
};

} // namespace ult
} // namespace L0
