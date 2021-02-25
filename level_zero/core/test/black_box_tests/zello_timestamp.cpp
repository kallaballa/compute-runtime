/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "zello_common.h"

extern bool verbose;
bool verbose = false;

inline std::vector<uint8_t> loadBinaryFile(const std::string &filePath) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.good()) {
        std::cerr << "Failed to load binary file: " << filePath << " " << strerror(errno) << "\n";
        return {};
    }

    stream.seekg(0, stream.end);
    const size_t length = static_cast<size_t>(stream.tellg());
    stream.seekg(0, stream.beg);

    std::vector<uint8_t> binary_file(length);
    stream.read(reinterpret_cast<char *>(binary_file.data()), length);
    return binary_file;
}

void createCmdQueueAndCmdList(ze_context_handle_t &context,
                              ze_device_handle_t &device,
                              ze_command_queue_handle_t &cmdQueue,
                              ze_command_list_handle_t &cmdList) {
    // Create command queue
    uint32_t numQueueGroups = 0;
    SUCCESS_OR_TERMINATE(zeDeviceGetCommandQueueGroupProperties(device, &numQueueGroups, nullptr));
    if (numQueueGroups == 0) {
        std::cout << "No queue groups found!\n";
        std::terminate();
    }
    std::vector<ze_command_queue_group_properties_t> queueProperties(numQueueGroups);
    SUCCESS_OR_TERMINATE(zeDeviceGetCommandQueueGroupProperties(device, &numQueueGroups,
                                                                queueProperties.data()));

    ze_command_queue_desc_t cmdQueueDesc = {};
    for (uint32_t i = 0; i < numQueueGroups; i++) {
        if (queueProperties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) {
            cmdQueueDesc.ordinal = i;
            break;
        }
    }
    cmdQueueDesc.index = 0;
    cmdQueueDesc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
    SUCCESS_OR_TERMINATE(zeCommandQueueCreate(context, device, &cmdQueueDesc, &cmdQueue));

    // Create command list
    ze_command_list_desc_t cmdListDesc = {};
    cmdListDesc.commandQueueGroupOrdinal = cmdQueueDesc.ordinal;
    SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &cmdListDesc, &cmdList));
}

void createEventPoolAndEvents(ze_context_handle_t &context,
                              ze_device_handle_t &device,
                              ze_event_pool_handle_t &eventPool,
                              ze_event_pool_flag_t poolFlag,
                              uint32_t poolSize,
                              ze_event_handle_t *events) {
    ze_event_pool_desc_t eventPoolDesc;
    ze_event_desc_t eventDesc;
    eventPoolDesc.count = poolSize;
    eventPoolDesc.flags = poolFlag;
    SUCCESS_OR_TERMINATE(zeEventPoolCreate(context, &eventPoolDesc, 1, &device, &eventPool));

    for (uint32_t i = 0; i < poolSize; i++) {
        eventDesc.index = i;
        eventDesc.signal = ZE_EVENT_SCOPE_FLAG_HOST;
        eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;
        SUCCESS_OR_TERMINATE(zeEventCreate(eventPool, &eventDesc, events + i));
    }
}

bool testWriteGlobalTimestamp(ze_context_handle_t &context,
                              ze_driver_handle_t &driver,
                              ze_device_handle_t &device) {
    constexpr size_t allocSize = 4096;
    constexpr size_t tsAllocSize = 64;
    ze_command_queue_handle_t cmdQueue;
    ze_command_list_handle_t cmdList;
    uint64_t tsStartResult = 0, tsEndResult = 0;

    void *dstBuffer;
    void *globalTsStart, *globalTsEnd;

    // Create commandQueue and cmdList
    createCmdQueueAndCmdList(context, device, cmdQueue, cmdList);

    // Alloc buffers
    dstBuffer = nullptr;
    globalTsStart = nullptr;
    globalTsEnd = nullptr;

    ze_device_mem_alloc_desc_t deviceDesc = {};
    deviceDesc.flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED;
    deviceDesc.ordinal = 0;

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &deviceDesc, allocSize, 1, device, &dstBuffer));
    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &deviceDesc, tsAllocSize, 1, device, &globalTsStart));
    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &deviceDesc, tsAllocSize, 1, device, &globalTsEnd));

    // Init data and copy to device
    uint8_t initDataDst[allocSize];
    memset(initDataDst, 3, sizeof(initDataDst));

    SUCCESS_OR_TERMINATE(zeCommandListAppendWriteGlobalTimestamp(cmdList, (uint64_t *)globalTsStart, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(cmdList, dstBuffer, initDataDst,
                                                       sizeof(initDataDst), nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(cmdList, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendWriteGlobalTimestamp(cmdList, (uint64_t *)globalTsEnd, nullptr, 0, nullptr));

    // Copy back timestamp data
    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(cmdList, &tsStartResult, globalTsStart, sizeof(tsStartResult),
                                                       nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(cmdList, &tsEndResult, globalTsEnd, sizeof(tsEndResult),
                                                       nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListClose(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(cmdQueue, 1, &cmdList, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(cmdQueue, std::numeric_limits<uint32_t>::max()));

    ze_device_properties_t devProperties = {};
    SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

    uint64_t commandDuration = tsEndResult - tsStartResult;
    uint64_t timerResolution = devProperties.timerResolution;
    std::cout << "Global timestamp statistics: \n"
              << std::fixed
              << " Command start : " << std::dec << tsStartResult << " cycles\n"
              << " Command end : " << std::dec << tsEndResult << " cycles\n"
              << " Command duration : " << std::dec << commandDuration << " cycles, " << commandDuration * timerResolution << " ns\n";

    // Tear down

    SUCCESS_OR_TERMINATE(zeMemFree(context, dstBuffer));
    SUCCESS_OR_TERMINATE(zeMemFree(context, globalTsStart));
    SUCCESS_OR_TERMINATE(zeMemFree(context, globalTsEnd));
    SUCCESS_OR_TERMINATE(zeCommandListDestroy(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(cmdQueue));

    return true;
}

bool testKernelTimestampHostQuery(ze_context_handle_t &context,
                                  ze_driver_handle_t &driver,
                                  ze_device_handle_t &device) {

    ze_command_queue_handle_t cmdQueue;
    ze_command_list_handle_t cmdList;

    // Create commandQueue and cmdList
    createCmdQueueAndCmdList(context, device, cmdQueue, cmdList);

    // Create two shared buffers
    constexpr size_t allocSize = 4096;
    ze_device_mem_alloc_desc_t deviceDesc = {};
    deviceDesc.flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED;
    deviceDesc.ordinal = 0;

    ze_host_mem_alloc_desc_t hostDesc = {};
    hostDesc.flags = ZE_HOST_MEM_ALLOC_FLAG_BIAS_UNCACHED;

    void *srcBuffer = nullptr;
    SUCCESS_OR_TERMINATE(zeMemAllocShared(context, &deviceDesc, &hostDesc, allocSize, 1, device, &srcBuffer));

    void *dstBuffer = nullptr;
    SUCCESS_OR_TERMINATE(zeMemAllocShared(context, &deviceDesc, &hostDesc, allocSize, 1, device, &dstBuffer));

    // Initialize memory
    constexpr uint8_t val = 55;
    memset(srcBuffer, val, allocSize);
    memset(dstBuffer, 0, allocSize);

    // Create kernel
    auto spirvModule = loadBinaryFile("copy_buffer_to_buffer.spv");
    if (spirvModule.size() == 0) {
        return false;
    }

    ze_module_handle_t module;
    ze_kernel_handle_t kernel;
    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(spirvModule.data());
    moduleDesc.inputSize = spirvModule.size();
    SUCCESS_OR_TERMINATE(zeModuleCreate(context, device, &moduleDesc, &module, nullptr));

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = "CopyBufferToBufferBytes";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernelDesc, &kernel));

    uint32_t groupSizeX = 32u;
    uint32_t groupSizeY = 1u;
    uint32_t groupSizeZ = 1u;
    SUCCESS_OR_TERMINATE(zeKernelSuggestGroupSize(kernel, allocSize, 1U, 1U, &groupSizeX, &groupSizeY, &groupSizeZ));
    SUCCESS_OR_TERMINATE(zeKernelSetGroupSize(kernel, groupSizeX, groupSizeY, groupSizeZ));

    uint32_t offset = 0;
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 1, sizeof(dstBuffer), &dstBuffer));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 0, sizeof(srcBuffer), &srcBuffer));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 2, sizeof(uint32_t), &offset));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 3, sizeof(uint32_t), &offset));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 4, sizeof(uint32_t), &offset));

    ze_group_count_t dispatchTraits;
    dispatchTraits.groupCountX = allocSize / groupSizeX;
    dispatchTraits.groupCountY = 1u;
    dispatchTraits.groupCountZ = 1u;

    ze_event_pool_handle_t eventPool;
    ze_event_handle_t kernelTsEvent;
    createEventPoolAndEvents(context, device, eventPool, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, 1, &kernelTsEvent);

    SUCCESS_OR_TERMINATE(zeCommandListAppendLaunchKernel(cmdList, kernel, &dispatchTraits, kernelTsEvent, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListClose(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(cmdQueue, 1, &cmdList, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(cmdQueue, std::numeric_limits<uint32_t>::max()));

    ze_kernel_timestamp_result_t kernelTsResults;
    SUCCESS_OR_TERMINATE(zeEventQueryKernelTimestamp(kernelTsEvent, &kernelTsResults));

    ze_device_properties_t devProperties = {};
    SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

    uint64_t timerResolution = devProperties.timerResolution;
    uint64_t kernelDuration = kernelTsResults.context.kernelEnd - kernelTsResults.context.kernelStart;
    std::cout << "Kernel timestamp statistics: \n"
              << std::fixed
              << " Global start : " << std::dec << kernelTsResults.global.kernelStart << " cycles\n"
              << " Kernel start: " << std::dec << kernelTsResults.context.kernelStart << " cycles\n"
              << " Kernel end: " << std::dec << kernelTsResults.context.kernelEnd << " cycles\n"
              << " Global end: " << std::dec << kernelTsResults.global.kernelEnd << " cycles\n"
              << " Kernel duration : " << std::dec << kernelDuration << " cycles, " << kernelDuration * timerResolution << " ns\n";

    // Cleanup
    SUCCESS_OR_TERMINATE(zeMemFree(context, dstBuffer));
    SUCCESS_OR_TERMINATE(zeMemFree(context, srcBuffer));
    SUCCESS_OR_TERMINATE(zeEventDestroy(kernelTsEvent));
    SUCCESS_OR_TERMINATE(zeEventPoolDestroy(eventPool));
    SUCCESS_OR_TERMINATE(zeCommandListDestroy(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(cmdQueue));
    return true;
}

bool testKernelTimestampApendQuery(ze_context_handle_t &context,
                                   ze_device_handle_t &device) {

    ze_command_queue_handle_t cmdQueue;
    ze_command_list_handle_t cmdList;

    // Create commandQueue and cmdList
    createCmdQueueAndCmdList(context, device, cmdQueue, cmdList);

    // Create two shared buffers
    constexpr size_t allocSize = 4096;
    ze_device_mem_alloc_desc_t deviceDesc = {};
    deviceDesc.flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED;
    deviceDesc.ordinal = 0;

    ze_host_mem_alloc_desc_t hostDesc = {};
    hostDesc.flags = ZE_HOST_MEM_ALLOC_FLAG_BIAS_UNCACHED;

    void *srcBuffer = nullptr;
    SUCCESS_OR_TERMINATE(zeMemAllocShared(context, &deviceDesc, &hostDesc, allocSize, 1, device, &srcBuffer));

    void *dstBuffer = nullptr;
    SUCCESS_OR_TERMINATE(zeMemAllocShared(context, &deviceDesc, &hostDesc, allocSize, 1, device, &dstBuffer));

    void *timestampBuffer = nullptr;
    SUCCESS_OR_TERMINATE(zeMemAllocHost(context, &hostDesc, sizeof(ze_kernel_timestamp_result_t), 1, &timestampBuffer));

    // Initialize memory
    constexpr uint8_t val = 55;
    memset(srcBuffer, val, allocSize);
    memset(dstBuffer, 0, allocSize);
    memset(timestampBuffer, 0, sizeof(ze_kernel_timestamp_result_t));

    // Create kernel
    auto spirvModule = loadBinaryFile("copy_buffer_to_buffer.spv");
    if (spirvModule.size() == 0) {
        return false;
    }

    ze_module_handle_t module;
    ze_kernel_handle_t kernel;
    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(spirvModule.data());
    moduleDesc.inputSize = spirvModule.size();
    SUCCESS_OR_TERMINATE(zeModuleCreate(context, device, &moduleDesc, &module, nullptr));

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = "CopyBufferToBufferBytes";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernelDesc, &kernel));

    uint32_t groupSizeX = 32u;
    uint32_t groupSizeY = 1u;
    uint32_t groupSizeZ = 1u;
    SUCCESS_OR_TERMINATE(zeKernelSuggestGroupSize(kernel, allocSize, 1U, 1U, &groupSizeX, &groupSizeY, &groupSizeZ));
    SUCCESS_OR_TERMINATE(zeKernelSetGroupSize(kernel, groupSizeX, groupSizeY, groupSizeZ));

    uint32_t offset = 0;
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 1, sizeof(dstBuffer), &dstBuffer));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 0, sizeof(srcBuffer), &srcBuffer));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 2, sizeof(uint32_t), &offset));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 3, sizeof(uint32_t), &offset));
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(kernel, 4, sizeof(uint32_t), &offset));

    ze_group_count_t dispatchTraits;
    dispatchTraits.groupCountX = allocSize / groupSizeX;
    dispatchTraits.groupCountY = 1u;
    dispatchTraits.groupCountZ = 1u;

    ze_event_pool_handle_t eventPool;
    ze_event_handle_t kernelTsEvent;
    createEventPoolAndEvents(context, device, eventPool, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, 1, &kernelTsEvent);

    SUCCESS_OR_TERMINATE(zeCommandListAppendLaunchKernel(cmdList, kernel, &dispatchTraits, kernelTsEvent, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(cmdList, nullptr, 0u, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendQueryKernelTimestamps(cmdList, 1u, &kernelTsEvent, timestampBuffer, nullptr, nullptr, 0u, nullptr));

    SUCCESS_OR_TERMINATE(zeCommandListClose(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(cmdQueue, 1, &cmdList, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(cmdQueue, std::numeric_limits<uint32_t>::max()));

    ze_kernel_timestamp_result_t *kernelTsResults = reinterpret_cast<ze_kernel_timestamp_result_t *>(timestampBuffer);

    ze_device_properties_t devProperties = {};
    SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

    uint64_t timerResolution = devProperties.timerResolution;
    uint64_t kernelDuration = kernelTsResults->context.kernelEnd - kernelTsResults->context.kernelStart;
    std::cout << "Kernel timestamp statistics: \n"
              << std::fixed
              << " Global start : " << std::dec << kernelTsResults->global.kernelStart << " cycles\n"
              << " Kernel start: " << std::dec << kernelTsResults->context.kernelStart << " cycles\n"
              << " Kernel end: " << std::dec << kernelTsResults->context.kernelEnd << " cycles\n"
              << " Global end: " << std::dec << kernelTsResults->global.kernelEnd << " cycles\n"
              << " Kernel duration : " << std::dec << kernelDuration << " cycles, " << kernelDuration * timerResolution << " ns\n";

    // Cleanup
    SUCCESS_OR_TERMINATE(zeMemFree(context, dstBuffer));
    SUCCESS_OR_TERMINATE(zeMemFree(context, srcBuffer));
    SUCCESS_OR_TERMINATE(zeMemFree(context, timestampBuffer));
    SUCCESS_OR_TERMINATE(zeEventDestroy(kernelTsEvent));
    SUCCESS_OR_TERMINATE(zeEventPoolDestroy(eventPool));
    SUCCESS_OR_TERMINATE(zeCommandListDestroy(cmdList));
    SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(cmdQueue));
    return true;
}

void printResult(bool result, std::string &currentTest) {
    std::cout << "\nZello Timestamp: " << currentTest.c_str()
              << "  Results validation "
              << (result ? "PASSED" : "FAILED")
              << std::endl;
}

int main(int argc, char *argv[]) {
    verbose = isVerbose(argc, argv);
    ze_context_handle_t context = nullptr;
    auto device = zelloInitContextAndGetDevices(context);

    ze_device_properties_t deviceProperties = {};
    SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &deviceProperties));
    std::cout << "Device : \n"
              << " * name : " << deviceProperties.name << "\n"
              << " * vendorId : " << std::hex << deviceProperties.vendorId << "\n";

    bool result;
    std::string currentTest;

    currentTest = "Test Append Write of Global Timestamp";
    result = testKernelTimestampApendQuery(context, device);
    printResult(result, currentTest);

    SUCCESS_OR_TERMINATE(zeContextDestroy(context));

    return result ? 0 : 1;
}
