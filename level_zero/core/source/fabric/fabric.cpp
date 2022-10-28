/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/fabric/fabric.h"

#include "shared/source/helpers/string.h"

#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/core/source/fabric/fabric_device_interface.h"

namespace L0 {

FabricVertex::~FabricVertex() {

    for (auto subVertex : subVertices) {
        delete subVertex;
    }
    subVertices.clear();
}

FabricVertex *FabricVertex::createFromDevice(Device *device) {

    // Fabric Vertices are not created for engine instanced devices
    if (device->getNEODevice()->isEngineInstanced()) {
        return nullptr;
    }

    auto fabricVertex = new FabricVertex();
    UNRECOVERABLE_IF(fabricVertex == nullptr);

    auto deviceImpl = static_cast<DeviceImp *>(device);
    for (auto &subDevice : deviceImpl->subDevices) {
        auto subVertex = FabricVertex::createFromDevice(subDevice);
        if (subVertex == nullptr) {
            continue;
        }
        auto subDeviceImpl = static_cast<DeviceImp *>(subDevice);
        subDeviceImpl->setFabricVertex(subVertex);
        fabricVertex->subVertices.push_back(subVertex);
    }

    ze_device_properties_t deviceProperties = {};
    ze_pci_ext_properties_t pciProperties = {};

    deviceProperties.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
    pciProperties.stype = ZE_STRUCTURE_TYPE_PCI_EXT_PROPERTIES;

    device->getProperties(&deviceProperties);

    fabricVertex->properties.stype = ZE_STRUCTURE_TYPE_FABRIC_VERTEX_EXP_PROPERTIES;
    memcpy_s(fabricVertex->properties.uuid.id, ZE_MAX_UUID_SIZE, deviceProperties.uuid.id, ZE_MAX_DEVICE_UUID_SIZE);

    if (deviceProperties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) {
        fabricVertex->properties.type = ZE_FABRIC_VERTEX_EXP_TYPE_SUBEVICE;
    } else {
        fabricVertex->properties.type = ZE_FABRIC_VERTEX_EXP_TYPE_DEVICE;
    }
    fabricVertex->properties.remote = false;
    fabricVertex->device = device;
    if (device->getPciProperties(&pciProperties) == ZE_RESULT_SUCCESS) {
        fabricVertex->properties.address.domain = pciProperties.address.domain;
        fabricVertex->properties.address.bus = pciProperties.address.bus;
        fabricVertex->properties.address.device = pciProperties.address.device;
        fabricVertex->properties.address.function = pciProperties.address.function;
    }

    fabricVertex->pFabricDeviceInterface = FabricDeviceInterface::createFabricDeviceInterface(*fabricVertex);
    UNRECOVERABLE_IF(fabricVertex->pFabricDeviceInterface == nullptr);

    fabricVertex->pFabricDeviceInterface->enumerate();

    return fabricVertex;
}

ze_result_t FabricVertex::getSubVertices(uint32_t *pCount, ze_fabric_vertex_handle_t *phSubvertices) {

    uint32_t subVertexCount = static_cast<uint32_t>(subVertices.size());
    if (*pCount == 0) {
        *pCount = subVertexCount;
        return ZE_RESULT_SUCCESS;
    }

    *pCount = std::min(subVertexCount, *pCount);
    for (uint32_t index = 0; index < *pCount; index++) {
        phSubvertices[index] = subVertices[index]->toHandle();
    }

    return ZE_RESULT_SUCCESS;
}

ze_result_t FabricVertex::getProperties(ze_fabric_vertex_exp_properties_t *pVertexProperties) const {
    *pVertexProperties = properties;
    return ZE_RESULT_SUCCESS;
}

ze_result_t FabricVertex::getDevice(ze_device_handle_t *phDevice) const {

    *phDevice = device->toHandle();
    return ZE_RESULT_SUCCESS;
}

} // namespace L0
