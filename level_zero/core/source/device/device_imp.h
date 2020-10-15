/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "level_zero/core/source/builtin/builtin_functions_lib.h"
#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/driver/driver_handle.h"
#include "level_zero/tools/source/metrics/metric.h"

namespace L0 {
struct SysmanDevice;
struct DeviceImp : public Device {
    uint32_t getRootDeviceIndex() override;
    ze_result_t canAccessPeer(ze_device_handle_t hPeerDevice, ze_bool_t *value) override;
    ze_result_t createCommandList(const ze_command_list_desc_t *desc,
                                  ze_command_list_handle_t *commandList) override;
    ze_result_t createCommandListImmediate(const ze_command_queue_desc_t *desc,
                                           ze_command_list_handle_t *phCommandList) override;
    ze_result_t createCommandQueue(const ze_command_queue_desc_t *desc,
                                   ze_command_queue_handle_t *commandQueue) override;
    ze_result_t createImage(const ze_image_desc_t *desc, ze_image_handle_t *phImage) override;
    ze_result_t createModule(const ze_module_desc_t *desc, ze_module_handle_t *module,
                             ze_module_build_log_handle_t *buildLog) override;
    ze_result_t createSampler(const ze_sampler_desc_t *pDesc,
                              ze_sampler_handle_t *phSampler) override;
    ze_result_t getComputeProperties(ze_device_compute_properties_t *pComputeProperties) override;
    ze_result_t getP2PProperties(ze_device_handle_t hPeerDevice,
                                 ze_device_p2p_properties_t *pP2PProperties) override;
    ze_result_t getKernelProperties(ze_device_module_properties_t *pKernelProperties) override;
    ze_result_t getMemoryProperties(uint32_t *pCount, ze_device_memory_properties_t *pMemProperties) override;
    ze_result_t getMemoryAccessProperties(ze_device_memory_access_properties_t *pMemAccessProperties) override;
    ze_result_t getProperties(ze_device_properties_t *pDeviceProperties) override;
    ze_result_t getSubDevices(uint32_t *pCount, ze_device_handle_t *phSubdevices) override;
    ze_result_t setIntermediateCacheConfig(ze_cache_config_flags_t cacheConfig) override;
    ze_result_t setLastLevelCacheConfig(ze_cache_config_flags_t cacheConfig) override;
    ze_result_t getCacheProperties(uint32_t *pCount, ze_device_cache_properties_t *pCacheProperties) override;
    ze_result_t imageGetProperties(const ze_image_desc_t *desc, ze_image_properties_t *pImageProperties) override;
    ze_result_t getDeviceImageProperties(ze_device_image_properties_t *pDeviceImageProperties) override;
    ze_result_t getCommandQueueGroupProperties(uint32_t *pCount,
                                               ze_command_queue_group_properties_t *pCommandQueueGroupProperties) override;
    ze_result_t systemBarrier() override;
    void *getExecEnvironment() override;
    BuiltinFunctionsLib *getBuiltinFunctionsLib() override;
    uint32_t getMOCS(bool l3enabled, bool l1enabled) override;
    NEO::HwHelper &getHwHelper() override;
    bool isMultiDeviceCapable() const override;
    const NEO::HardwareInfo &getHwInfo() const override;
    NEO::OSInterface &getOsInterface() override;
    uint32_t getPlatformInfo() const override;
    MetricContext &getMetricContext() override;
    uint32_t getMaxNumHwThreads() const override;
    ze_result_t registerCLMemory(cl_context context, cl_mem mem, void **ptr) override;
    ze_result_t registerCLProgram(cl_context context, cl_program program,
                                  ze_module_handle_t *phModule) override;
    ze_result_t registerCLCommandQueue(cl_context context, cl_command_queue commandQueue,
                                       ze_command_queue_handle_t *phCommandQueue) override;
    ze_result_t activateMetricGroups(uint32_t count,
                                     zet_metric_group_handle_t *phMetricGroups) override;

    DriverHandle *getDriverHandle() override;
    void setDriverHandle(DriverHandle *driverHandle) override;
    NEO::PreemptionMode getDevicePreemptionMode() const override;
    const NEO::DeviceInfo &getDeviceInfo() const override;

    NEO::Device *getNEODevice() override;
    void activateMetricGroups() override;
    void processAdditionalKernelProperties(NEO::HwHelper &hwHelper, ze_device_module_properties_t *pKernelProperties);
    NEO::GraphicsAllocation *getDebugSurface() const override { return debugSurface; }
    void setDebugSurface(NEO::GraphicsAllocation *debugSurface) { this->debugSurface = debugSurface; };
    ~DeviceImp() override;
    NEO::GraphicsAllocation *allocateManagedMemoryFromHostPtr(void *buffer, size_t size, struct CommandList *commandList) override;
    NEO::GraphicsAllocation *allocateMemoryFromHostPtr(const void *buffer, size_t size) override;
    void setSysmanHandle(SysmanDevice *pSysman) override;
    SysmanDevice *getSysmanHandle() override;
    ze_result_t getCsrForOrdinalAndIndex(NEO::CommandStreamReceiver **csr, uint32_t ordinal, uint32_t index) override;
    ze_result_t mapOrdinalForAvailableEngineGroup(uint32_t *ordinal) override;

    NEO::Device *neoDevice = nullptr;
    bool isSubdevice = false;
    void *execEnvironment = nullptr;
    std::unique_ptr<BuiltinFunctionsLib> builtins = nullptr;
    std::unique_ptr<MetricContext> metricContext = nullptr;
    uint32_t maxNumHwThreads = 0;
    uint32_t numSubDevices = 0;
    std::vector<Device *> subDevices;
    DriverHandle *driverHandle = nullptr;
    CommandList *pageFaultCommandList = nullptr;

    bool resourcesReleased = false;
    void releaseResources();

  protected:
    NEO::GraphicsAllocation *debugSurface = nullptr;
    SysmanDevice *pSysmanDevice = nullptr;
};

} // namespace L0
