/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/context/context_imp.h"

#include "shared/source/memory_manager/memory_operations_handler.h"
#include "shared/source/memory_manager/unified_memory_manager.h"

#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/image/image.h"
#include "level_zero/core/source/memory/memory_operations_helper.h"

namespace L0 {

ze_result_t ContextImp::destroy() {
    delete this;

    return ZE_RESULT_SUCCESS;
}

ze_result_t ContextImp::getStatus() {
    DriverHandleImp *driverHandleImp = static_cast<DriverHandleImp *>(this->driverHandle);
    for (auto device : driverHandleImp->devices) {
        DeviceImp *deviceImp = static_cast<DeviceImp *>(device);
        if (deviceImp->resourcesReleased) {
            return ZE_RESULT_ERROR_DEVICE_LOST;
        }
    }
    return ZE_RESULT_SUCCESS;
}

DriverHandle *ContextImp::getDriverHandle() {
    return this->driverHandle;
}

ContextImp::ContextImp(DriverHandle *driverHandle) {
    this->driverHandle = static_cast<DriverHandleImp *>(driverHandle);
}

void ContextImp::addDeviceAndSubDevices(Device *device) {
    this->devices.insert(std::make_pair(device->toHandle(), device));
    DeviceImp *deviceImp = static_cast<DeviceImp *>(device);
    for (auto subDevice : deviceImp->subDevices) {
        this->addDeviceAndSubDevices(subDevice);
    }
}

ze_result_t ContextImp::allocHostMem(const ze_host_mem_alloc_desc_t *hostDesc,
                                     size_t size,
                                     size_t alignment,
                                     void **ptr) {

    NEO::SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::HOST_UNIFIED_MEMORY,
                                                                           this->rootDeviceIndices,
                                                                           this->deviceBitfields);

    auto usmPtr = this->driverHandle->svmAllocsManager->createHostUnifiedMemoryAllocation(size,
                                                                                          unifiedMemoryProperties);
    if (usmPtr == nullptr) {
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    *ptr = usmPtr;

    return ZE_RESULT_SUCCESS;
}

bool ContextImp::isDeviceDefinedForThisContext(Device *inDevice) {
    return (this->getDevices().find(inDevice->toHandle()) != this->getDevices().end());
}

ze_result_t ContextImp::allocDeviceMem(ze_device_handle_t hDevice,
                                       const ze_device_mem_alloc_desc_t *deviceDesc,
                                       size_t size,
                                       size_t alignment, void **ptr) {

    if (isDeviceDefinedForThisContext(Device::fromHandle(hDevice)) == false) {
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    bool relaxedSizeAllowed = false;
    if (deviceDesc->pNext) {
        const ze_base_desc_t *extendedDesc = reinterpret_cast<const ze_base_desc_t *>(deviceDesc->pNext);
        if (extendedDesc->stype == ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC) {
            const ze_external_memory_export_desc_t *externalMemoryExportDesc =
                reinterpret_cast<const ze_external_memory_export_desc_t *>(extendedDesc);
            // ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF supported by default for all
            // device allocations for the purpose of IPC, so just check correct
            // flag is passed.
            if (externalMemoryExportDesc->flags & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD) {
                return ZE_RESULT_ERROR_UNSUPPORTED_ENUMERATION;
            }
        } else if (extendedDesc->stype == ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD) {
            const ze_external_memory_import_fd_t *externalMemoryImportDesc =
                reinterpret_cast<const ze_external_memory_import_fd_t *>(extendedDesc);
            if (externalMemoryImportDesc->flags & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD) {
                return ZE_RESULT_ERROR_UNSUPPORTED_ENUMERATION;
            }
            ze_ipc_memory_flags_t flags = {};
            *ptr = this->driverHandle->importFdHandle(hDevice, flags, externalMemoryImportDesc->fd);
            if (nullptr == *ptr) {
                return ZE_RESULT_ERROR_INVALID_ARGUMENT;
            }
            return ZE_RESULT_SUCCESS;
        } else if (extendedDesc->stype == ZE_STRUCTURE_TYPE_RELAXED_ALLOCATION_LIMITS_EXP_DESC) {
            const ze_relaxed_allocation_limits_exp_desc_t *relaxedLimitsDesc =
                reinterpret_cast<const ze_relaxed_allocation_limits_exp_desc_t *>(extendedDesc);
            if (!(relaxedLimitsDesc->flags & ZE_RELAXED_ALLOCATION_LIMITS_EXP_FLAG_MAX_SIZE)) {
                return ZE_RESULT_ERROR_INVALID_ARGUMENT;
            }
            relaxedSizeAllowed = true;
        }
    }

    if (relaxedSizeAllowed == false &&
        (size > this->driverHandle->devices[0]->getNEODevice()->getHardwareCapabilities().maxMemAllocSize)) {
        *ptr = nullptr;
        return ZE_RESULT_ERROR_UNSUPPORTED_SIZE;
    }

    auto neoDevice = Device::fromHandle(hDevice)->getNEODevice();
    auto rootDeviceIndex = neoDevice->getRootDeviceIndex();
    auto deviceBitfields = this->driverHandle->deviceBitfields;

    deviceBitfields[rootDeviceIndex] = neoDevice->getDeviceBitfield();

    NEO::SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::DEVICE_UNIFIED_MEMORY, this->driverHandle->rootDeviceIndices, deviceBitfields);
    unifiedMemoryProperties.allocationFlags.flags.shareable = 1u;
    unifiedMemoryProperties.device = neoDevice;

    if (deviceDesc->flags & ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED) {
        unifiedMemoryProperties.allocationFlags.flags.locallyUncachedResource = 1;
    }

    void *usmPtr =
        this->driverHandle->svmAllocsManager->createUnifiedMemoryAllocation(size, unifiedMemoryProperties);
    if (usmPtr == nullptr) {
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    *ptr = usmPtr;

    return ZE_RESULT_SUCCESS;
}

ze_result_t ContextImp::allocSharedMem(ze_device_handle_t hDevice,
                                       const ze_device_mem_alloc_desc_t *deviceDesc,
                                       const ze_host_mem_alloc_desc_t *hostDesc,
                                       size_t size,
                                       size_t alignment,
                                       void **ptr) {
    bool relaxedSizeAllowed = false;
    if (deviceDesc->pNext) {
        const ze_base_desc_t *extendedDesc = reinterpret_cast<const ze_base_desc_t *>(deviceDesc->pNext);
        if (extendedDesc->stype == ZE_STRUCTURE_TYPE_RELAXED_ALLOCATION_LIMITS_EXP_DESC) {
            const ze_relaxed_allocation_limits_exp_desc_t *relaxedLimitsDesc =
                reinterpret_cast<const ze_relaxed_allocation_limits_exp_desc_t *>(extendedDesc);
            if (!(relaxedLimitsDesc->flags & ZE_RELAXED_ALLOCATION_LIMITS_EXP_FLAG_MAX_SIZE)) {
                return ZE_RESULT_ERROR_INVALID_ARGUMENT;
            }
            relaxedSizeAllowed = true;
        }
    }

    if (relaxedSizeAllowed == false &&
        (size > this->devices.begin()->second->getNEODevice()->getHardwareCapabilities().maxMemAllocSize)) {
        *ptr = nullptr;
        return ZE_RESULT_ERROR_UNSUPPORTED_SIZE;
    }

    auto neoDevice = this->devices.begin()->second->getNEODevice();

    auto deviceBitfields = this->deviceBitfields;
    NEO::Device *unifiedMemoryPropertiesDevice = nullptr;
    if (hDevice) {
        if (isDeviceDefinedForThisContext(Device::fromHandle(hDevice)) == false) {
            return ZE_RESULT_ERROR_DEVICE_LOST;
        }

        neoDevice = Device::fromHandle(hDevice)->getNEODevice();
        auto rootDeviceIndex = neoDevice->getRootDeviceIndex();
        unifiedMemoryPropertiesDevice = neoDevice;
        deviceBitfields[rootDeviceIndex] = neoDevice->getDeviceBitfield();
    }

    NEO::SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::SHARED_UNIFIED_MEMORY,
                                                                           this->rootDeviceIndices,
                                                                           deviceBitfields);
    unifiedMemoryProperties.device = unifiedMemoryPropertiesDevice;

    if (deviceDesc->flags & ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED) {
        unifiedMemoryProperties.allocationFlags.flags.locallyUncachedResource = 1;
    }

    auto usmPtr =
        this->driverHandle->svmAllocsManager->createSharedUnifiedMemoryAllocation(size,
                                                                                  unifiedMemoryProperties,
                                                                                  static_cast<void *>(neoDevice->getSpecializedDevice<L0::Device>()));

    if (usmPtr == nullptr) {
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    *ptr = usmPtr;

    return ZE_RESULT_SUCCESS;
}

ze_result_t ContextImp::freeMem(const void *ptr) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->freeMem(ptr);
}

ze_result_t ContextImp::makeMemoryResident(ze_device_handle_t hDevice, void *ptr, size_t size) {
    Device *device = L0::Device::fromHandle(hDevice);
    NEO::Device *neoDevice = device->getNEODevice();
    auto allocation = device->getDriverHandle()->getDriverSystemMemoryAllocation(
        ptr,
        size,
        neoDevice->getRootDeviceIndex(),
        nullptr);
    if (allocation == nullptr) {
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    NEO::MemoryOperationsHandler *memoryOperationsIface = neoDevice->getRootDeviceEnvironment().memoryOperationsInterface.get();
    auto success = memoryOperationsIface->makeResident(neoDevice, ArrayRef<NEO::GraphicsAllocation *>(&allocation, 1));
    ze_result_t res = changeMemoryOperationStatusToL0ResultType(success);

    if (ZE_RESULT_SUCCESS == res) {
        auto allocData = device->getDriverHandle()->getSvmAllocsManager()->getSVMAlloc(ptr);
        if (allocData && allocData->memoryType == InternalMemoryType::SHARED_UNIFIED_MEMORY) {
            DriverHandleImp *driverHandleImp = static_cast<DriverHandleImp *>(device->getDriverHandle());
            std::lock_guard<std::mutex> lock(driverHandleImp->sharedMakeResidentAllocationsLock);
            driverHandleImp->sharedMakeResidentAllocations.insert({ptr, allocation});
        }
    }

    return res;
}

ze_result_t ContextImp::evictMemory(ze_device_handle_t hDevice, void *ptr, size_t size) {
    Device *device = L0::Device::fromHandle(hDevice);
    NEO::Device *neoDevice = device->getNEODevice();
    auto allocation = device->getDriverHandle()->getDriverSystemMemoryAllocation(
        ptr,
        size,
        neoDevice->getRootDeviceIndex(),
        nullptr);
    if (allocation == nullptr) {
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    {
        DriverHandleImp *driverHandleImp = static_cast<DriverHandleImp *>(device->getDriverHandle());
        std::lock_guard<std::mutex> lock(driverHandleImp->sharedMakeResidentAllocationsLock);
        driverHandleImp->sharedMakeResidentAllocations.erase(ptr);
    }

    NEO::MemoryOperationsHandler *memoryOperationsIface = neoDevice->getRootDeviceEnvironment().memoryOperationsInterface.get();
    auto success = memoryOperationsIface->evict(neoDevice, *allocation);
    return changeMemoryOperationStatusToL0ResultType(success);
}

ze_result_t ContextImp::makeImageResident(ze_device_handle_t hDevice, ze_image_handle_t hImage) {
    auto alloc = Image::fromHandle(hImage)->getAllocation();

    NEO::Device *neoDevice = L0::Device::fromHandle(hDevice)->getNEODevice();
    NEO::MemoryOperationsHandler *memoryOperationsIface = neoDevice->getRootDeviceEnvironment().memoryOperationsInterface.get();
    auto success = memoryOperationsIface->makeResident(neoDevice, ArrayRef<NEO::GraphicsAllocation *>(&alloc, 1));
    return changeMemoryOperationStatusToL0ResultType(success);
}
ze_result_t ContextImp::evictImage(ze_device_handle_t hDevice, ze_image_handle_t hImage) {
    auto alloc = Image::fromHandle(hImage)->getAllocation();

    NEO::Device *neoDevice = L0::Device::fromHandle(hDevice)->getNEODevice();
    NEO::MemoryOperationsHandler *memoryOperationsIface = neoDevice->getRootDeviceEnvironment().memoryOperationsInterface.get();
    auto success = memoryOperationsIface->evict(neoDevice, *alloc);
    return changeMemoryOperationStatusToL0ResultType(success);
}

ze_result_t ContextImp::getMemAddressRange(const void *ptr,
                                           void **pBase,
                                           size_t *pSize) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->getMemAddressRange(ptr,
                                                  pBase,
                                                  pSize);
}

ze_result_t ContextImp::closeIpcMemHandle(const void *ptr) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->closeIpcMemHandle(ptr);
}

ze_result_t ContextImp::getIpcMemHandle(const void *ptr,
                                        ze_ipc_mem_handle_t *pIpcHandle) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->getIpcMemHandle(ptr,
                                               pIpcHandle);
}

ze_result_t ContextImp::openIpcMemHandle(ze_device_handle_t hDevice,
                                         ze_ipc_mem_handle_t handle,
                                         ze_ipc_memory_flags_t flags,
                                         void **ptr) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->openIpcMemHandle(hDevice,
                                                handle,
                                                flags,
                                                ptr);
}

ze_result_t ContextImp::getMemAllocProperties(const void *ptr,
                                              ze_memory_allocation_properties_t *pMemAllocProperties,
                                              ze_device_handle_t *phDevice) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->getMemAllocProperties(ptr,
                                                     pMemAllocProperties,
                                                     phDevice);
}

ze_result_t ContextImp::createModule(ze_device_handle_t hDevice,
                                     const ze_module_desc_t *desc,
                                     ze_module_handle_t *phModule,
                                     ze_module_build_log_handle_t *phBuildLog) {
    return L0::Device::fromHandle(hDevice)->createModule(desc, phModule, phBuildLog, ModuleType::User);
}

ze_result_t ContextImp::createSampler(ze_device_handle_t hDevice,
                                      const ze_sampler_desc_t *pDesc,
                                      ze_sampler_handle_t *phSampler) {
    return L0::Device::fromHandle(hDevice)->createSampler(pDesc, phSampler);
}

ze_result_t ContextImp::createCommandQueue(ze_device_handle_t hDevice,
                                           const ze_command_queue_desc_t *desc,
                                           ze_command_queue_handle_t *commandQueue) {
    return L0::Device::fromHandle(hDevice)->createCommandQueue(desc, commandQueue);
}

ze_result_t ContextImp::createCommandList(ze_device_handle_t hDevice,
                                          const ze_command_list_desc_t *desc,
                                          ze_command_list_handle_t *commandList) {
    return L0::Device::fromHandle(hDevice)->createCommandList(desc, commandList);
}

ze_result_t ContextImp::createCommandListImmediate(ze_device_handle_t hDevice,
                                                   const ze_command_queue_desc_t *desc,
                                                   ze_command_list_handle_t *commandList) {
    return L0::Device::fromHandle(hDevice)->createCommandListImmediate(desc, commandList);
}

ze_result_t ContextImp::activateMetricGroups(zet_device_handle_t hDevice,
                                             uint32_t count,
                                             zet_metric_group_handle_t *phMetricGroups) {
    return L0::Device::fromHandle(hDevice)->activateMetricGroups(count, phMetricGroups);
}

ze_result_t ContextImp::reserveVirtualMem(const void *pStart,
                                          size_t size,
                                          void **pptr) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::freeVirtualMem(const void *ptr,
                                       size_t size) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::queryVirtualMemPageSize(ze_device_handle_t hDevice,
                                                size_t size,
                                                size_t *pagesize) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::createPhysicalMem(ze_device_handle_t hDevice,
                                          ze_physical_mem_desc_t *desc,
                                          ze_physical_mem_handle_t *phPhysicalMemory) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::destroyPhysicalMem(ze_physical_mem_handle_t hPhysicalMemory) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::mapVirtualMem(const void *ptr,
                                      size_t size,
                                      ze_physical_mem_handle_t hPhysicalMemory,
                                      size_t offset,
                                      ze_memory_access_attribute_t access) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::unMapVirtualMem(const void *ptr,
                                        size_t size) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::setVirtualMemAccessAttribute(const void *ptr,
                                                     size_t size,
                                                     ze_memory_access_attribute_t access) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::getVirtualMemAccessAttribute(const void *ptr,
                                                     size_t size,
                                                     ze_memory_access_attribute_t *access,
                                                     size_t *outSize) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t ContextImp::openEventPoolIpcHandle(ze_ipc_event_pool_handle_t hIpc,
                                               ze_event_pool_handle_t *phEventPool) {
    DEBUG_BREAK_IF(nullptr == this->driverHandle);
    return this->driverHandle->openEventPoolIpcHandle(hIpc, phEventPool);
}

ze_result_t ContextImp::createEventPool(const ze_event_pool_desc_t *desc,
                                        uint32_t numDevices,
                                        ze_device_handle_t *phDevices,
                                        ze_event_pool_handle_t *phEventPool) {
    EventPool *eventPool = EventPool::create(this->driverHandle, this, numDevices, phDevices, desc);

    if (eventPool == nullptr) {
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    *phEventPool = eventPool->toHandle();

    return ZE_RESULT_SUCCESS;
}

ze_result_t ContextImp::createImage(ze_device_handle_t hDevice,
                                    const ze_image_desc_t *desc,
                                    ze_image_handle_t *phImage) {
    return L0::Device::fromHandle(hDevice)->createImage(desc, phImage);
}

} // namespace L0
