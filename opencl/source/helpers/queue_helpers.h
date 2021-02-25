/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/device/device.h"
#include "shared/source/helpers/get_info.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/command_queue/command_queue.h"
#include "opencl/source/device_queue/device_queue.h"
#include "opencl/source/helpers/get_info_status_mapper.h"

namespace NEO {

inline void releaseVirtualEvent(CommandQueue &commandQueue) {
    if (commandQueue.getRefApiCount() == 1) {
        commandQueue.releaseVirtualEvent();
    }
}

inline void releaseVirtualEvent(DeviceQueue &commandQueue) {
}

bool isCommandWithoutKernel(uint32_t commandType);

template <typename QueueType>
void retainQueue(cl_command_queue commandQueue, cl_int &retVal) {
    using BaseType = typename QueueType::BaseType;
    auto queue = castToObject<QueueType>(static_cast<BaseType *>(commandQueue));
    if (queue) {
        queue->retain();
        retVal = CL_SUCCESS;
    }
}

template <typename QueueType>
void releaseQueue(cl_command_queue commandQueue, cl_int &retVal) {
    using BaseType = typename QueueType::BaseType;
    auto queue = castToObject<QueueType>(static_cast<BaseType *>(commandQueue));
    if (queue) {
        releaseVirtualEvent(*queue);
        queue->release();
        retVal = CL_SUCCESS;
    }
}

template <>
inline void releaseQueue<CommandQueue>(cl_command_queue commandQueue, cl_int &retVal) {
    using BaseType = typename CommandQueue::BaseType;
    auto queue = castToObject<CommandQueue>(static_cast<BaseType *>(commandQueue));
    if (queue) {
        queue->flush();
        releaseVirtualEvent(*queue);
        queue->release();
        retVal = CL_SUCCESS;
    }
}

void getIntelQueueInfo(CommandQueue *queue, cl_command_queue_info paramName, GetInfoHelper &getInfoHelper, cl_int &retVal);

inline void getHostQueueInfo(CommandQueue *queue, cl_command_queue_info paramName, GetInfoHelper &getInfoHelper, cl_int &retVal) {
    switch (paramName) {
    case CL_QUEUE_FAMILY_INTEL:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_uint>(queue->getQueueFamilyIndex()));
        break;
    case CL_QUEUE_INDEX_INTEL:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_uint>(queue->getQueueIndexWithinFamily()));
        break;
    default:
        getIntelQueueInfo(queue, paramName, getInfoHelper, retVal);
        break;
    }
}

template <typename QueueType>
cl_int getQueueInfo(QueueType *queue,
                    cl_command_queue_info paramName,
                    size_t paramValueSize,
                    void *paramValue,
                    size_t *paramValueSizeRet) {

    cl_int retVal = CL_SUCCESS;
    GetInfoHelper getInfoHelper(paramValue, paramValueSize, paramValueSizeRet);

    switch (paramName) {
    case CL_QUEUE_CONTEXT:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_context>(&queue->getContext()));
        break;
    case CL_QUEUE_DEVICE: {
        Device &device = queue->getDevice();
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_device_id>(device.getSpecializedDevice<ClDevice>()));
        break;
    }
    case CL_QUEUE_REFERENCE_COUNT:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_int>(queue->getReference()));
        break;
    case CL_QUEUE_PROPERTIES:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_command_queue_properties>(queue->getCommandQueueProperties()));
        break;
    case CL_QUEUE_DEVICE_DEFAULT:
        retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_command_queue>(queue->getContext().getDefaultDeviceQueue()));
        break;
    case CL_QUEUE_SIZE:
        if (std::is_same<QueueType, class DeviceQueue>::value) {
            auto devQ = reinterpret_cast<DeviceQueue *>(queue);
            retVal = changeGetInfoStatusToCLResultType(getInfoHelper.set<cl_uint>(devQ->getQueueSize()));
            break;
        }
        retVal = CL_INVALID_COMMAND_QUEUE;
        break;
    case CL_QUEUE_PROPERTIES_ARRAY: {
        auto &propertiesVector = queue->getPropertiesVector();
        auto source = propertiesVector.data();
        auto sourceSize = propertiesVector.size() * sizeof(cl_queue_properties);
        auto getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, source, sourceSize);
        retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
        GetInfo::setParamValueReturnSize(paramValueSizeRet, sourceSize, getInfoStatus);
        break;
    }
    default:
        if (std::is_same<QueueType, class CommandQueue>::value) {
            auto cmdQ = reinterpret_cast<CommandQueue *>(queue);
            getHostQueueInfo(cmdQ, paramName, getInfoHelper, retVal);
            break;
        }
        retVal = CL_INVALID_VALUE;
        break;
    }

    return retVal;
}

template <typename QueueType>
void getQueueInfo(cl_command_queue commandQueue,
                  cl_command_queue_info paramName,
                  size_t paramValueSize,
                  void *paramValue,
                  size_t *paramValueSizeRet,
                  cl_int &retVal) {
    using BaseType = typename QueueType::BaseType;
    auto queue = castToObject<QueueType>(static_cast<BaseType *>(commandQueue));
    if (queue) {
        retVal = getQueueInfo<QueueType>(queue, paramName, paramValueSize, paramValue, paramValueSizeRet);
    }
}

template <typename returnType>
returnType getCmdQueueProperties(const cl_queue_properties *properties,
                                 cl_queue_properties propertyName = CL_QUEUE_PROPERTIES,
                                 bool *foundValue = nullptr) {
    if (properties != nullptr) {
        while (*properties != 0) {
            if (*properties == propertyName) {
                if (foundValue) {
                    *foundValue = true;
                }
                return static_cast<returnType>(*(properties + 1));
            }
            properties += 2;
        }
    }

    if (foundValue) {
        *foundValue = false;
    }
    return 0;
}
bool isExtraToken(const cl_queue_properties *property);
bool verifyExtraTokens(ClDevice *&device, Context &context, const cl_queue_properties *properties);
} // namespace NEO
