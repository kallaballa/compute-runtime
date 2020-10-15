/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/get_info.h"

#include "shared/source/device/device.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/context/context.h"
#include "opencl/source/helpers/base_object.h"
#include "opencl/source/helpers/get_info_status_mapper.h"
#include "opencl/source/helpers/validators.h"
#include "opencl/source/program/kernel_info.h"

#include "program.h"

namespace NEO {

cl_int Program::getInfo(cl_program_info paramName, size_t paramValueSize,
                        void *paramValue, size_t *paramValueSizeRet) {
    cl_int retVal = CL_SUCCESS;
    const void *pSrc = nullptr;
    size_t srcSize = GetInfo::invalidSourceSize;
    size_t retSize = 0;
    std::string kernelNamesString;
    cl_device_id device_id = pDevice->getSpecializedDevice<ClDevice>();
    cl_uint refCount = 0;
    size_t numKernels;
    cl_context clContext = context;
    cl_uint clFalse = CL_FALSE;
    auto rootDeviceIndex = pDevice->getRootDeviceIndex();

    switch (paramName) {
    case CL_PROGRAM_CONTEXT:
        pSrc = &clContext;
        retSize = srcSize = sizeof(clContext);
        break;

    case CL_PROGRAM_BINARIES:
        packDeviceBinary(rootDeviceIndex);
        pSrc = buildInfos[rootDeviceIndex].packedDeviceBinary.get();
        retSize = sizeof(void **);
        srcSize = buildInfos[rootDeviceIndex].packedDeviceBinarySize;
        if (paramValue != nullptr) {
            if (paramValueSize < retSize) {
                retVal = CL_INVALID_VALUE;
                break;
            }
            paramValueSize = srcSize;
            paramValue = *(void **)paramValue;
        }
        break;

    case CL_PROGRAM_BINARY_SIZES:
        packDeviceBinary(rootDeviceIndex);
        pSrc = &buildInfos[rootDeviceIndex].packedDeviceBinarySize;
        retSize = srcSize = sizeof(size_t *);
        break;

    case CL_PROGRAM_KERNEL_NAMES:
        kernelNamesString = concatenateKernelNames(kernelInfoArray);
        pSrc = kernelNamesString.c_str();
        retSize = srcSize = kernelNamesString.length() + 1;

        if (buildStatus != CL_BUILD_SUCCESS) {
            retVal = CL_INVALID_PROGRAM_EXECUTABLE;
        }
        break;

    case CL_PROGRAM_NUM_KERNELS:
        numKernels = kernelInfoArray.size();
        pSrc = &numKernels;
        retSize = srcSize = sizeof(numKernels);

        if (buildStatus != CL_BUILD_SUCCESS) {
            retVal = CL_INVALID_PROGRAM_EXECUTABLE;
        }
        break;

    case CL_PROGRAM_NUM_DEVICES:
        pSrc = &numDevices;
        retSize = srcSize = sizeof(cl_uint);
        break;

    case CL_PROGRAM_DEVICES:
        pSrc = &device_id;
        retSize = srcSize = sizeof(cl_device_id);
        break;

    case CL_PROGRAM_REFERENCE_COUNT:
        refCount = static_cast<cl_uint>(this->getReference());
        retSize = srcSize = sizeof(refCount);
        pSrc = &refCount;
        break;

    case CL_PROGRAM_SOURCE:
        if (createdFrom == CreatedFrom::SOURCE) {
            pSrc = sourceCode.c_str();
            retSize = srcSize = strlen(sourceCode.c_str()) + 1;
        } else {
            if (paramValueSizeRet) {
                *paramValueSizeRet = 0;
            }
            return CL_SUCCESS;
        }
        break;

    case CL_PROGRAM_IL:
        if (createdFrom != CreatedFrom::IL) {
            if (paramValueSizeRet) {
                *paramValueSizeRet = 0;
            }
            return CL_SUCCESS;
        }
        pSrc = irBinary.get();
        retSize = srcSize = irBinarySize;
        break;

    case CL_PROGRAM_DEBUG_INFO_SIZES_INTEL:
        retSize = srcSize = sizeof(debugDataSize);
        pSrc = &debugDataSize;
        break;

    case CL_PROGRAM_DEBUG_INFO_INTEL:
        pSrc = debugData.get();
        retSize = numDevices * sizeof(void **);
        srcSize = debugDataSize;
        if (paramValue != nullptr) {
            if (paramValueSize < retSize) {
                retVal = CL_INVALID_VALUE;
                break;
            }
            paramValueSize = srcSize;
            paramValue = *(void **)paramValue;
        }
        break;

    case CL_PROGRAM_SCOPE_GLOBAL_CTORS_PRESENT:
    case CL_PROGRAM_SCOPE_GLOBAL_DTORS_PRESENT:
        retSize = srcSize = sizeof(clFalse);
        pSrc = &clFalse;
        break;

    default:
        retVal = CL_INVALID_VALUE;
        break;
    }

    auto getInfoStatus = GetInfoStatus::INVALID_VALUE;
    if (retVal == CL_SUCCESS) {
        getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, pSrc, srcSize);
        retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
    }
    GetInfo::setParamValueReturnSize(paramValueSizeRet, retSize, getInfoStatus);
    return retVal;
}

cl_int Program::getBuildInfo(cl_device_id device, cl_program_build_info paramName,
                             size_t paramValueSize, void *paramValue, size_t *paramValueSizeRet) const {
    cl_int retVal = CL_SUCCESS;
    const void *pSrc = nullptr;
    size_t srcSize = GetInfo::invalidSourceSize;
    size_t retSize = 0;
    cl_device_id device_id = pDevice->getSpecializedDevice<ClDevice>();

    if (device != device_id) {
        return CL_INVALID_DEVICE;
    }

    auto pClDev = castToObject<ClDevice>(device);
    auto rootDeviceIndex = pClDev->getRootDeviceIndex();

    switch (paramName) {
    case CL_PROGRAM_BUILD_STATUS:
        srcSize = retSize = sizeof(cl_build_status);
        pSrc = &buildStatus;
        break;

    case CL_PROGRAM_BUILD_OPTIONS:
        srcSize = retSize = strlen(options.c_str()) + 1;
        pSrc = options.c_str();
        break;

    case CL_PROGRAM_BUILD_LOG: {
        const char *pBuildLog = getBuildLog(pClDev->getRootDeviceIndex());

        pSrc = pBuildLog;
        srcSize = retSize = strlen(pBuildLog) + 1;
    } break;

    case CL_PROGRAM_BINARY_TYPE:
        srcSize = retSize = sizeof(cl_program_binary_type);
        pSrc = &programBinaryType;
        break;

    case CL_PROGRAM_BUILD_GLOBAL_VARIABLE_TOTAL_SIZE:
        pSrc = &buildInfos[rootDeviceIndex].globalVarTotalSize;
        retSize = srcSize = sizeof(size_t);
        break;

    default:
        retVal = CL_INVALID_VALUE;
        break;
    }

    auto getInfoStatus = GetInfoStatus::INVALID_VALUE;
    if (retVal == CL_SUCCESS) {
        getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, pSrc, srcSize);
        retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
    }
    GetInfo::setParamValueReturnSize(paramValueSizeRet, retSize, getInfoStatus);

    return retVal;
}
} // namespace NEO
