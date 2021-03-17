/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/device.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/source/os_interface/linux/sys_calls.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/platform/platform.h"
#include "opencl/source/sharings/va/va_device.h"

#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <va/va_backend.h>

namespace NEO {
ClDevice *VADevice::getRootDeviceFromVaDisplay(Platform *pPlatform, VADisplay vaDisplay) {
    VADisplayContextP pDisplayContext_test = reinterpret_cast<VADisplayContextP>(vaDisplay);
    UNRECOVERABLE_IF(pDisplayContext_test->vadpy_magic != 0x56414430);
    VADriverContextP pDriverContext_test = pDisplayContext_test->pDriverContext;
    int deviceFd = *static_cast<int *>(pDriverContext_test->drm_state);

    UNRECOVERABLE_IF(deviceFd < 0);

    char path[256] = {0};
    size_t pathlen = 256;

    if (SysCalls::getDevicePath(deviceFd, path, pathlen)) {
        return nullptr;
    }

    if (SysCalls::access(path, F_OK)) {
        return nullptr;
    }

    int readLinkSize = 0;
    char devicePath[256] = {0};
    readLinkSize = SysCalls::readlink(path, devicePath, pathlen);

    if (readLinkSize == -1) {
        return nullptr;
    }

    std::string_view devicePathView(devicePath, static_cast<size_t>(readLinkSize));
    devicePathView = devicePathView.substr(devicePathView.find("/drm/render") - 7u, 7u);

    for (size_t i = 0; i < pPlatform->getNumDevices(); ++i) {
        auto device = pPlatform->getClDevice(i);
        NEO::Device *neoDevice = &device->getDevice();

        auto *drm = neoDevice->getRootDeviceEnvironment().osInterface->get()->getDrm();
        auto pciPath = drm->getPciPath();
        if (devicePathView == pciPath) {
            return device;
        }
    }
    return nullptr;
}
} // namespace NEO
