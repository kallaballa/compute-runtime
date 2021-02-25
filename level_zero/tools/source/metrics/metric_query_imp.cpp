/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/tools/source/metrics/metric_query_imp.h"

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/engine_node_helper.h"
#include "shared/source/memory_manager/allocation_properties.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/source/os_interface/os_library.h"

#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/tools/source/metrics/metric_enumeration_imp.h"

using namespace MetricsLibraryApi;

namespace L0 {

MetricsLibrary::MetricsLibrary(MetricContext &metricContextInput)
    : metricContext(metricContextInput) {}

MetricsLibrary::~MetricsLibrary() {
    release();
}

ze_result_t MetricsLibrary::getInitializationState() {
    return initializationState;
}

bool MetricsLibrary::isInitialized() {
    // Try to initialize metrics library only once.
    if (initializationState == ZE_RESULT_ERROR_UNINITIALIZED) {
        initialize();
    }

    return initializationState == ZE_RESULT_SUCCESS;
}

bool MetricsLibrary::createMetricQuery(const uint32_t slotsCount, QueryHandle_1_0 &query,
                                       NEO::GraphicsAllocation *&pAllocation) {
    std::lock_guard<std::mutex> lock(mutex);

    // Validate metrics library state.
    if (!isInitialized()) {
        DEBUG_BREAK_IF(true);
        return false;
    }

    TypedValue_1_0 gpuReportSize = {};
    QueryCreateData_1_0 queryData = {};
    queryData.HandleContext = context;
    queryData.Type = ObjectType::QueryHwCounters;
    queryData.Slots = slotsCount;

    // Obtain gpu report size.
    api.GetParameter(ParameterType::QueryHwCountersReportGpuSize, &gpuReportSize.Type,
                     &gpuReportSize);

    // Validate gpu report size.
    if (!gpuReportSize.ValueUInt32) {
        DEBUG_BREAK_IF(true);
        return false;
    }

    // Allocate gpu memory.
    NEO::AllocationProperties properties(
        metricContext.getDevice().getRootDeviceIndex(), gpuReportSize.ValueUInt32 * slotsCount, NEO::GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY, metricContext.getDevice().getNEODevice()->getDeviceBitfield());
    properties.alignment = 64u;
    pAllocation = metricContext.getDevice().getDriverHandle()->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);

    // Validate gpu report size.
    if (!pAllocation) {
        DEBUG_BREAK_IF(true);
        return false;
    }

    // Mark allocation as shared and clear it.
    memset(pAllocation->getUnderlyingBuffer(), 0, gpuReportSize.ValueUInt32 * slotsCount);

    // Create query pool within metrics library.
    if (api.QueryCreate(&queryData, &query) != StatusCode::Success) {
        DEBUG_BREAK_IF(true);
        metricContext.getDevice().getDriverHandle()->getMemoryManager()->freeGraphicsMemory(pAllocation);
        return false;
    }

    // Register created query.
    queries.push_back(query);

    return true;
}

uint32_t MetricsLibrary::getMetricQueryCount() {
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<uint32_t>(queries.size());
}

bool MetricsLibrary::destroyMetricQuery(QueryHandle_1_0 &query) {
    std::lock_guard<std::mutex> lock(mutex);
    DEBUG_BREAK_IF(!query.IsValid());

    const bool result = isInitialized() && (api.QueryDelete(query) == StatusCode::Success);
    auto iter = std::find_if(queries.begin(), queries.end(), [&](const QueryHandle_1_0 &element) { return element.data == query.data; });

    // Unregister query.
    if (iter != queries.end()) {
        queries.erase(iter);
    }

    return result;
}

bool MetricsLibrary::getMetricQueryReportSize(size_t &rawDataSize) {
    ValueType valueType = ValueType::Last;
    TypedValue_1_0 value = {};

    const bool result = isInitialized() && (api.GetParameter(ParameterType::QueryHwCountersReportApiSize, &valueType, &value) == StatusCode::Success);
    rawDataSize = static_cast<size_t>(value.ValueUInt32);
    DEBUG_BREAK_IF(!result);
    return result;
}

bool MetricsLibrary::getMetricQueryReport(QueryHandle_1_0 &query, const uint32_t slot,
                                          const size_t rawDataSize, uint8_t *pData) {

    GetReportData_1_0 report = {};
    report.Type = ObjectType::QueryHwCounters;
    report.Query.Handle = query;
    report.Query.Slot = slot;
    report.Query.SlotsCount = 1;
    report.Query.Data = pData;
    report.Query.DataSize = static_cast<uint32_t>(rawDataSize);

    const bool result = isInitialized() && (api.GetData(&report) == StatusCode::Success);
    DEBUG_BREAK_IF(!result);
    return result;
}

void MetricsLibrary::initialize() {
    auto &metricsEnumeration = metricContext.getMetricEnumeration();

    // Function should be called only once.
    DEBUG_BREAK_IF(initializationState != ZE_RESULT_ERROR_UNINITIALIZED);

    // Metrics Enumeration needs to be initialized before Metrics Library
    const bool validMetricsEnumeration = metricsEnumeration.isInitialized();
    const bool validMetricsLibrary = validMetricsEnumeration && handle && createContext();

    // Load metrics library and exported functions.
    initializationState = validMetricsLibrary ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
    DEBUG_BREAK_IF(initializationState != ZE_RESULT_SUCCESS);
}

void MetricsLibrary::release() {

    // Delete metric group configurations.
    deleteAllConfigurations();

    // Destroy context.
    if (context.IsValid() && contextDeleteFunction) {
        contextDeleteFunction(context);
    }

    // Reset metric query state to not initialized.
    api = {};
    callbacks = {};
    context = {};
    initializationState = ZE_RESULT_ERROR_UNINITIALIZED;
}

bool MetricsLibrary::load() {
    // Load library.
    handle = NEO::OsLibrary::load(getFilename());

    // Load exported functions.
    if (handle) {
        contextCreateFunction = reinterpret_cast<ContextCreateFunction_1_0>(
            handle->getProcAddress(METRICS_LIBRARY_CONTEXT_CREATE_1_0));
        contextDeleteFunction = reinterpret_cast<ContextDeleteFunction_1_0>(
            handle->getProcAddress(METRICS_LIBRARY_CONTEXT_DELETE_1_0));
    }

    if (contextCreateFunction == nullptr || contextDeleteFunction == nullptr) {
        PRINT_DEBUG_STRING(NEO::DebugManager.flags.PrintDebugMessages.get(), stderr, "cannot load %s exported functions\n", MetricsLibrary::getFilename());
        return false;
    }

    // Return success if exported functions have been loaded.
    return true;
}

bool MetricsLibrary::createContext() {
    auto &device = metricContext.getDevice();
    const auto &hwHelper = device.getHwHelper();
    const auto &asyncComputeEngines = hwHelper.getGpgpuEngineInstances(device.getHwInfo());
    ContextCreateData_1_0 createData = {};
    ClientOptionsData_1_0 clientOptions[2] = {};
    ClientData_1_0 clientData = {};
    ClientType_1_0 clientType = {};
    ClientDataLinuxAdapter_1_0 adapter = {};

    // Check if compute command streamer is used.
    auto asyncComputeEngine = std::find_if(asyncComputeEngines.begin(), asyncComputeEngines.end(), [&](const auto &engine) {
        return engine.first == aub_stream::ENGINE_CCS;
    });

    const auto &deviceImp = *static_cast<DeviceImp *>(&device);
    const auto &commandStreamReceiver = *deviceImp.neoDevice->getDefaultEngine().commandStreamReceiver;
    const auto engineType = commandStreamReceiver.getOsContext().getEngineType();
    const bool isComputeUsed = NEO::EngineHelpers::isCcs(engineType);

    metricContext.setUseCompute(isComputeUsed);

    // Create metrics library context.
    DEBUG_BREAK_IF(!contextCreateFunction);
    clientType.Api = ClientApi::OneApi;
    clientType.Gen = getGenType(device.getPlatformInfo());

    clientOptions[0].Type = ClientOptionsType::Compute;
    clientOptions[0].Compute.Asynchronous = asyncComputeEngine != asyncComputeEngines.end();

    clientOptions[1].Type = ClientOptionsType::Tbs;
    clientOptions[1].Tbs.Enabled = metricContext.getMetricStreamer() != nullptr;

    clientData.Linux.Adapter = &adapter;
    clientData.ClientOptions = clientOptions;
    clientData.ClientOptionsCount = sizeof(clientOptions) / sizeof(ClientOptionsData_1_0);

    createData.Api = &api;
    createData.ClientCallbacks = &callbacks;
    createData.ClientData = &clientData;

    const bool result =
        getContextData(device, createData) &&
        contextCreateFunction(clientType, &createData, &context) == StatusCode::Success;

    DEBUG_BREAK_IF(!result);
    return result;
}

ClientGen MetricsLibrary::getGenType(const uint32_t gen) const {
    auto &hwHelper = NEO::HwHelper::get(static_cast<GFXCORE_FAMILY>(gen));
    return static_cast<MetricsLibraryApi::ClientGen>(hwHelper.getMetricsLibraryGenId());
}

uint32_t MetricsLibrary::getGpuCommandsSize(CommandBufferData_1_0 &commandBuffer) {
    CommandBufferSize_1_0 commandBufferSize = {};

    bool result = isInitialized();

    // Validate metrics library initialization state.
    if (result) {
        commandBuffer.HandleContext = context;
        result = api.CommandBufferGetSize(&commandBuffer, &commandBufferSize) == StatusCode::Success;
    }

    DEBUG_BREAK_IF(!result);
    return result ? commandBufferSize.GpuMemorySize : 0;
}

bool MetricsLibrary::getGpuCommands(CommandList &commandList,
                                    CommandBufferData_1_0 &commandBuffer) {

    // Obtain required command buffer size.
    commandBuffer.Size = getGpuCommandsSize(commandBuffer);

    // Validate gpu commands size.
    if (!commandBuffer.Size) {
        DEBUG_BREAK_IF(true);
        return false;
    }

    // Allocate command buffer.
    auto stream = commandList.commandContainer.getCommandStream();
    auto buffer = stream->getSpace(commandBuffer.Size);

    // Validate command buffer space.
    if (!buffer) {
        DEBUG_BREAK_IF(true);
        return false;
    }

    // Fill attached command buffer with gpu commands.
    commandBuffer.Data = buffer;

    // Obtain gpu commands from metrics library.
    const bool result =
        isInitialized() && (api.CommandBufferGet(&commandBuffer) == StatusCode::Success);
    DEBUG_BREAK_IF(!result);
    return result;
}

ConfigurationHandle_1_0
MetricsLibrary::createConfiguration(const zet_metric_group_handle_t metricGroupHandle,
                                    const zet_metric_group_properties_t properties) {
    // Metric group internal data.
    auto metricGroup = MetricGroup::fromHandle(metricGroupHandle);
    auto metricGroupDummy = ConfigurationHandle_1_0{};
    DEBUG_BREAK_IF(!metricGroup);

    // Metrics library configuration creation data.
    ConfigurationHandle_1_0 handle = {};
    ConfigurationCreateData_1_0 handleData = {};
    handleData.HandleContext = context;
    handleData.Type = ObjectType::ConfigurationHwCountersOa;

    // Check supported sampling types.
    const bool validSampling =
        properties.samplingType == ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED ||
        properties.samplingType == ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED;

    // Activate metric group through metrics discovery to send metric group
    // configuration to kernel driver.
    const bool validActivate = isInitialized() && validSampling && metricGroup->activate();

    if (validActivate) {
        // Use metrics library to create configuration for the activated metric group.
        api.ConfigurationCreate(&handleData, &handle);

        // Use metrics discovery to deactivate metric group.
        metricGroup->deactivate();
    }

    return validActivate ? handle : metricGroupDummy;
}

ConfigurationHandle_1_0 MetricsLibrary::getConfiguration(zet_metric_group_handle_t handle) {

    auto iter = configurations.find(handle);
    auto configuration = (iter != end(configurations)) ? iter->second : addConfiguration(handle);

    DEBUG_BREAK_IF(!configuration.IsValid());
    return configuration;
}

ConfigurationHandle_1_0 MetricsLibrary::addConfiguration(zet_metric_group_handle_t handle) {
    ConfigurationHandle_1_0 libraryHandle = {};
    DEBUG_BREAK_IF(!handle);

    // Create metrics library configuration.
    auto metricGroup = MetricGroup::fromHandle(handle);
    auto properties = MetricGroup::getProperties(handle);
    auto configuration = createConfiguration(metricGroup, properties);

    // Cache configuration if valid.
    if (configuration.IsValid()) {
        libraryHandle = configuration;
        cacheConfiguration(handle, libraryHandle);
    }

    DEBUG_BREAK_IF(!libraryHandle.IsValid());
    return libraryHandle;
}

void MetricsLibrary::deleteAllConfigurations() {

    if (api.ConfigurationDelete) {
        for (auto &configuration : configurations) {
            if (configuration.second.IsValid()) {
                api.ConfigurationDelete(configuration.second);
            }
        }
    }

    configurations.clear();
}

ze_result_t metricQueryPoolCreate(zet_context_handle_t hContext, zet_device_handle_t hDevice, zet_metric_group_handle_t hMetricGroup,
                                  const zet_metric_query_pool_desc_t *pDesc, zet_metric_query_pool_handle_t *phMetricQueryPool) {

    auto device = Device::fromHandle(hDevice);
    auto &metricContext = device->getMetricContext();

    // Metric query cannot be used with streamer simultaneously
    // (due to oa buffer usage constraints).
    if (metricContext.getMetricStreamer() != nullptr) {
        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    // Create metric query pool
    *phMetricQueryPool = MetricQueryPool::create(hDevice, hMetricGroup, *pDesc);

    // Return result status.
    return (*phMetricQueryPool != nullptr) ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_INVALID_ARGUMENT;
}

MetricQueryPool *MetricQueryPool::create(zet_device_handle_t hDevice,
                                         zet_metric_group_handle_t hMetricGroup,
                                         const zet_metric_query_pool_desc_t &desc) {
    auto device = Device::fromHandle(hDevice);
    auto metricPoolImp = new MetricQueryPoolImp(device->getMetricContext(), hMetricGroup, desc);

    if (!metricPoolImp->create()) {
        delete metricPoolImp;
        metricPoolImp = nullptr;
    }

    return metricPoolImp;
}

MetricQueryPoolImp::MetricQueryPoolImp(MetricContext &metricContextInput,
                                       zet_metric_group_handle_t hEventMetricGroupInput,
                                       const zet_metric_query_pool_desc_t &poolDescription)
    : metricContext(metricContextInput), metricsLibrary(metricContext.getMetricsLibrary()),
      description(poolDescription),
      hMetricGroup(hEventMetricGroupInput) {}

bool MetricQueryPoolImp::create() {
    switch (description.type) {
    case ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE:
        return createMetricQueryPool();
    case ZET_METRIC_QUERY_POOL_TYPE_EXECUTION:
        return createSkipExecutionQueryPool();
    default:
        DEBUG_BREAK_IF(true);
        return false;
    }
}

ze_result_t MetricQueryPoolImp::destroy() {
    switch (description.type) {
    case ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE:
        DEBUG_BREAK_IF(!(pAllocation && query.IsValid()));
        metricContext.getDevice().getDriverHandle()->getMemoryManager()->freeGraphicsMemory(pAllocation);
        metricsLibrary.destroyMetricQuery(query);
        delete this;
        break;
    case ZET_METRIC_QUERY_POOL_TYPE_EXECUTION:
        delete this;
        break;
    default:
        DEBUG_BREAK_IF(true);
        break;
    }

    return ZE_RESULT_SUCCESS;
}

bool MetricQueryPoolImp::createMetricQueryPool() {
    // Validate metric group query - only event based is supported.
    auto metricGroupProperites = MetricGroup::getProperties(hMetricGroup);
    const bool validMetricGroup = metricGroupProperites.samplingType == ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_EVENT_BASED;

    if (!validMetricGroup) {
        return false;
    }

    // Pool initialization.
    pool.reserve(description.count);
    for (uint32_t i = 0; i < description.count; ++i) {
        pool.push_back({metricContext, *this, i});
    }

    // Metrics library query object initialization.
    return metricsLibrary.createMetricQuery(description.count, query, pAllocation);
}

bool MetricQueryPoolImp::createSkipExecutionQueryPool() {

    pool.reserve(description.count);
    for (uint32_t i = 0; i < description.count; ++i) {
        pool.push_back({metricContext, *this, i});
    }

    return true;
}

MetricQueryPool *MetricQueryPool::fromHandle(zet_metric_query_pool_handle_t handle) {
    return static_cast<MetricQueryPool *>(handle);
}

zet_metric_query_pool_handle_t MetricQueryPool::toHandle() { return this; }

ze_result_t MetricQueryPoolImp::createMetricQuery(uint32_t index,
                                                  zet_metric_query_handle_t *phMetricQuery) {
    *phMetricQuery = (index < description.count)
                         ? &(pool[index])
                         : nullptr;

    return (*phMetricQuery != nullptr)
               ? ZE_RESULT_SUCCESS
               : ZE_RESULT_ERROR_INVALID_ARGUMENT;
}

MetricQueryImp::MetricQueryImp(MetricContext &metricContextInput, MetricQueryPoolImp &poolInput,
                               const uint32_t slotInput)
    : metricContext(metricContextInput), metricsLibrary(metricContext.getMetricsLibrary()),
      pool(poolInput), slot(slotInput) {}

ze_result_t MetricQueryImp::appendBegin(CommandList &commandList) {
    switch (pool.description.type) {
    case ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE:
        return writeMetricQuery(commandList, nullptr, 0, nullptr, true);
    case ZET_METRIC_QUERY_POOL_TYPE_EXECUTION:
        return writeSkipExecutionQuery(commandList, nullptr, 0, nullptr, true);
    default:
        DEBUG_BREAK_IF(true);
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }
}

ze_result_t MetricQueryImp::appendEnd(CommandList &commandList, ze_event_handle_t hSignalEvent,
                                      uint32_t numWaitEvents, ze_event_handle_t *phWaitEvents) {
    switch (pool.description.type) {
    case ZET_METRIC_QUERY_POOL_TYPE_PERFORMANCE:
        return writeMetricQuery(commandList, hSignalEvent, numWaitEvents, phWaitEvents, false);
    case ZET_METRIC_QUERY_POOL_TYPE_EXECUTION:
        return writeSkipExecutionQuery(commandList, hSignalEvent, numWaitEvents, phWaitEvents, false);
    default:
        DEBUG_BREAK_IF(true);
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }
}

ze_result_t MetricQueryImp::getData(size_t *pRawDataSize, uint8_t *pRawData) {

    const bool calculateSizeOnly = *pRawDataSize == 0;
    const bool result = calculateSizeOnly
                            ? metricsLibrary.getMetricQueryReportSize(*pRawDataSize)
                            : metricsLibrary.getMetricQueryReport(pool.query, slot, *pRawDataSize, pRawData);

    return result
               ? ZE_RESULT_SUCCESS
               : ZE_RESULT_ERROR_UNKNOWN;
}

ze_result_t MetricQueryImp::reset() {
    return ZE_RESULT_SUCCESS;
}

ze_result_t MetricQueryImp::destroy() {
    return ZE_RESULT_SUCCESS;
}

ze_result_t MetricQueryImp::writeMetricQuery(CommandList &commandList, ze_event_handle_t hSignalEvent,
                                             uint32_t numWaitEvents, ze_event_handle_t *phWaitEvents,
                                             const bool begin) {

    bool writeCompletionEvent = hSignalEvent && !begin;
    bool result = false;

    // Make gpu allocation visible.
    commandList.commandContainer.addToResidencyContainer(pool.pAllocation);

    // Obtain gpu commands.
    CommandBufferData_1_0 commandBuffer = {};
    commandBuffer.CommandsType = ObjectType::QueryHwCounters;
    commandBuffer.QueryHwCounters.Handle = pool.query;
    commandBuffer.QueryHwCounters.Begin = begin;
    commandBuffer.QueryHwCounters.Slot = slot;
    commandBuffer.Allocation.GpuAddress = pool.pAllocation->getGpuAddress();
    commandBuffer.Allocation.CpuAddress = pool.pAllocation->getUnderlyingBuffer();
    commandBuffer.Type = metricContext.isComputeUsed()
                             ? GpuCommandBufferType::Compute
                             : GpuCommandBufferType::Render;

    // Wait for events before executing query.
    result = zeCommandListAppendWaitOnEvents(commandList.toHandle(), numWaitEvents, phWaitEvents) ==
             ZE_RESULT_SUCCESS;

    // Get query commands.
    if (result) {
        result = metricsLibrary.getGpuCommands(commandList, commandBuffer);
    }

    // Write completion event.
    if (result && writeCompletionEvent) {
        result = zeCommandListAppendSignalEvent(commandList.toHandle(), hSignalEvent) ==
                 ZE_RESULT_SUCCESS;
    }

    return result ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
}

ze_result_t MetricQueryImp::writeSkipExecutionQuery(CommandList &commandList, ze_event_handle_t hSignalEvent,
                                                    uint32_t numWaitEvents, ze_event_handle_t *phWaitEvents,
                                                    const bool begin) {

    bool writeCompletionEvent = hSignalEvent && !begin;
    bool result = false;

    // Obtain gpu commands.
    CommandBufferData_1_0 commandBuffer = {};
    commandBuffer.CommandsType = ObjectType::OverrideNullHardware;
    commandBuffer.Override.Enable = begin;
    commandBuffer.Type = metricContext.isComputeUsed()
                             ? GpuCommandBufferType::Compute
                             : GpuCommandBufferType::Render;

    // Wait for events before executing query.
    zeCommandListAppendWaitOnEvents(commandList.toHandle(), numWaitEvents, phWaitEvents);

    // Get query commands.
    result = metricsLibrary.getGpuCommands(commandList, commandBuffer);

    // Write completion event.
    if (result && writeCompletionEvent) {
        result = zeCommandListAppendSignalEvent(commandList.toHandle(), hSignalEvent) ==
                 ZE_RESULT_SUCCESS;
    }

    return result ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
}

ze_result_t MetricQuery::appendMemoryBarrier(CommandList &commandList) {
    auto &metricContext = commandList.device->getMetricContext();
    auto &metricsLibrary = metricContext.getMetricsLibrary();

    // Obtain gpu commands.
    CommandBufferData_1_0 commandBuffer = {};
    commandBuffer.CommandsType = ObjectType::OverrideFlushCaches;
    commandBuffer.Override.Enable = true;
    commandBuffer.Type = metricContext.isComputeUsed()
                             ? GpuCommandBufferType::Compute
                             : GpuCommandBufferType::Render;

    return metricsLibrary.getGpuCommands(commandList, commandBuffer) ? ZE_RESULT_SUCCESS
                                                                     : ZE_RESULT_ERROR_UNKNOWN;
}

ze_result_t MetricQuery::appendStreamerMarker(CommandList &commandList,
                                              zet_metric_streamer_handle_t hMetricStreamer,
                                              uint32_t value) {

    auto &metricContext = commandList.device->getMetricContext();
    auto &metricsLibrary = metricContext.getMetricsLibrary();

    const uint32_t streamerMarkerHighBitsShift = 25;

    // Obtain gpu commands.
    CommandBufferData_1_0 commandBuffer = {};
    commandBuffer.CommandsType = ObjectType::MarkerStreamUser;
    commandBuffer.MarkerStreamUser.Value = value;
    commandBuffer.MarkerStreamUser.Reserved = (value >> streamerMarkerHighBitsShift);
    commandBuffer.Type = metricContext.isComputeUsed()
                             ? GpuCommandBufferType::Compute
                             : GpuCommandBufferType::Render;

    return metricsLibrary.getGpuCommands(commandList, commandBuffer) ? ZE_RESULT_SUCCESS
                                                                     : ZE_RESULT_ERROR_UNKNOWN;
}

MetricQuery *MetricQuery::fromHandle(zet_metric_query_handle_t handle) {
    return static_cast<MetricQuery *>(handle);
}

zet_metric_query_handle_t MetricQuery::toHandle() { return this; }

} // namespace L0
