/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/compiler_interface/compiler_interface.h"
#include "shared/source/device/device.h"
#include "shared/source/device_binary_format/device_binary_formats.h"
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/helpers/compiler_options_parser.h"
#include "shared/source/source_level_debugger/source_level_debugger.h"
#include "shared/source/utilities/time_measure_wrapper.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/gtpin/gtpin_notify.h"
#include "opencl/source/helpers/validators.h"
#include "opencl/source/platform/platform.h"
#include "opencl/source/program/kernel_info.h"
#include "opencl/source/program/program.h"
#include "opencl/source/utilities/logger.h"

#include "compiler_options.h"

#include <cstring>
#include <iterator>
#include <sstream>

namespace NEO {

cl_int Program::build(
    cl_uint numDevices,
    const cl_device_id *deviceList,
    const char *buildOptions,
    void(CL_CALLBACK *funcNotify)(cl_program program, void *userData),
    void *userData,
    bool enableCaching) {
    cl_int retVal = CL_SUCCESS;

    do {
        if (((deviceList == nullptr) && (numDevices != 0)) ||
            ((deviceList != nullptr) && (numDevices == 0))) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if ((funcNotify == nullptr) &&
            (userData != nullptr)) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        // if a device_list is specified, make sure it points to our device
        // NOTE: a null device_list is ok - it means "all devices"
        if (deviceList && validateObject(*deviceList) != CL_SUCCESS) {
            retVal = CL_INVALID_DEVICE;
            break;
        }

        // check to see if a previous build request is in progress
        if (buildStatus == CL_BUILD_IN_PROGRESS) {
            retVal = CL_INVALID_OPERATION;
            break;
        }

        if (isCreatedFromBinary == false) {
            buildStatus = CL_BUILD_IN_PROGRESS;

            if (nullptr != buildOptions) {
                options = buildOptions;
            } else if (this->createdFrom != CreatedFrom::BINARY) {
                options = "";
            }
            extractInternalOptions(options);
            applyAdditionalOptions();

            CompilerInterface *pCompilerInterface = pDevice->getCompilerInterface();
            if (!pCompilerInterface) {
                retVal = CL_OUT_OF_HOST_MEMORY;
                break;
            }

            TranslationInput inputArgs = {IGC::CodeType::oclC, IGC::CodeType::oclGenBin};
            if (createdFrom != CreatedFrom::SOURCE) {
                inputArgs.srcType = isSpirV ? IGC::CodeType::spirV : IGC::CodeType::llvmBc;
                inputArgs.src = ArrayRef<const char>(irBinary.get(), irBinarySize);
            } else {
                inputArgs.src = ArrayRef<const char>(sourceCode.c_str(), sourceCode.size());
            }

            if (inputArgs.src.size() == 0) {
                retVal = CL_INVALID_PROGRAM;
                break;
            }

            if (isKernelDebugEnabled()) {
                std::string filename;
                appendKernelDebugOptions();
                notifyDebuggerWithSourceCode(filename);
                if (!filename.empty()) {
                    // Add "-s" flag first so it will be ignored by clang in case the options already have this flag set.
                    options = std::string("-s ") + filename + " " + options;
                }
            }
            auto clDevice = this->pDevice->getSpecializedDevice<ClDevice>();
            UNRECOVERABLE_IF(clDevice == nullptr);

            if (requiresOpenClCFeatures(options)) {
                auto compilerExtensionsWithFeaturesOptions = clDevice->peekCompilerExtensionsWithFeatures();
                if (internalOptions.find(compilerExtensionsWithFeaturesOptions) == std::string::npos) {
                    CompilerOptions::concatenateAppend(internalOptions, compilerExtensionsWithFeaturesOptions);
                }
                auto compilerFeaturesOptions = clDevice->peekCompilerFeatures();
                if (internalOptions.find(compilerFeaturesOptions) == std::string::npos) {
                    CompilerOptions::concatenateAppend(internalOptions, compilerFeaturesOptions);
                }
            } else {
                auto compilerExtensionsOptions = clDevice->peekCompilerExtensions();
                if (internalOptions.find(compilerExtensionsOptions) == std::string::npos) {
                    CompilerOptions::concatenateAppend(internalOptions, compilerExtensionsOptions);
                }
            }

            inputArgs.apiOptions = ArrayRef<const char>(options.c_str(), options.length());
            inputArgs.internalOptions = ArrayRef<const char>(internalOptions.c_str(), internalOptions.length());
            inputArgs.GTPinInput = gtpinGetIgcInit();
            inputArgs.specializedValues = this->specConstantsValues;
            DBG_LOG(LogApiCalls,
                    "Build Options", inputArgs.apiOptions.begin(),
                    "\nBuild Internal Options", inputArgs.internalOptions.begin());
            inputArgs.allowCaching = enableCaching;
            NEO::TranslationOutput compilerOuput = {};
            auto compilerErr = pCompilerInterface->build(*this->pDevice, inputArgs, compilerOuput);
            this->updateBuildLog(this->pDevice->getRootDeviceIndex(), compilerOuput.frontendCompilerLog.c_str(), compilerOuput.frontendCompilerLog.size());
            this->updateBuildLog(this->pDevice->getRootDeviceIndex(), compilerOuput.backendCompilerLog.c_str(), compilerOuput.backendCompilerLog.size());
            retVal = asClError(compilerErr);
            if (retVal != CL_SUCCESS) {
                break;
            }
            if (inputArgs.srcType == IGC::CodeType::oclC) {
                this->irBinary = std::move(compilerOuput.intermediateRepresentation.mem);
                this->irBinarySize = compilerOuput.intermediateRepresentation.size;
                this->isSpirV = compilerOuput.intermediateCodeType == IGC::CodeType::spirV;
            }
            this->replaceDeviceBinary(std::move(compilerOuput.deviceBinary.mem), compilerOuput.deviceBinary.size, clDevice->getRootDeviceIndex());
            this->debugData = std::move(compilerOuput.debugData.mem);
            this->debugDataSize = compilerOuput.debugData.size;
        }
        updateNonUniformFlag();

        if (DebugManager.flags.PrintProgramBinaryProcessingTime.get()) {
            retVal = TimeMeasureWrapper::functionExecution(*this, &Program::processGenBinary, pDevice->getRootDeviceIndex());
        } else {
            retVal = processGenBinary(pDevice->getRootDeviceIndex());
        }

        if (retVal != CL_SUCCESS) {
            break;
        }

        if (isKernelDebugEnabled()) {
            processDebugData();
            auto clDevice = this->pDevice->getSpecializedDevice<ClDevice>();
            UNRECOVERABLE_IF(clDevice == nullptr);
            if (clDevice->getSourceLevelDebugger()) {
                for (auto kernelInfo : kernelInfoArray) {
                    clDevice->getSourceLevelDebugger()->notifyKernelDebugData(&kernelInfo->debugData,
                                                                              kernelInfo->name,
                                                                              kernelInfo->heapInfo.pKernelHeap,
                                                                              kernelInfo->heapInfo.KernelHeapSize);
                }
            }
        }

        separateBlockKernels();
    } while (false);

    if (retVal != CL_SUCCESS) {
        buildStatus = CL_BUILD_ERROR;
        programBinaryType = CL_PROGRAM_BINARY_TYPE_NONE;
    } else {
        buildStatus = CL_BUILD_SUCCESS;
        programBinaryType = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
    }

    if (funcNotify != nullptr) {
        (*funcNotify)(this, userData);
    }

    return retVal;
}

bool Program::appendKernelDebugOptions() {
    CompilerOptions::concatenateAppend(internalOptions, CompilerOptions::debugKernelEnable);
    CompilerOptions::concatenateAppend(options, CompilerOptions::generateDebugInfo);

    auto clDevice = this->pDevice->getSpecializedDevice<ClDevice>();
    UNRECOVERABLE_IF(clDevice == nullptr);

    auto debugger = clDevice->getSourceLevelDebugger();
    if (debugger && debugger->isOptimizationDisabled()) {
        CompilerOptions::concatenateAppend(options, CompilerOptions::optDisable);
    }
    return true;
}

void Program::notifyDebuggerWithSourceCode(std::string &filename) {
    auto clDevice = this->pDevice->getSpecializedDevice<ClDevice>();
    UNRECOVERABLE_IF(clDevice == nullptr);

    if (clDevice->getSourceLevelDebugger()) {
        clDevice->getSourceLevelDebugger()->notifySourceCode(sourceCode.c_str(), sourceCode.size(), filename);
    }
}

cl_int Program::build(const Device *pDevice, const char *buildOptions, bool enableCaching,
                      std::unordered_map<std::string, BuiltinDispatchInfoBuilder *> &builtinsMap) {
    cl_device_id deviceId = pDevice->getSpecializedDevice<ClDevice>();
    auto ret = this->build(1, &deviceId, buildOptions, nullptr, nullptr, enableCaching);
    if (ret != CL_SUCCESS) {
        return ret;
    }

    for (auto &ki : this->kernelInfoArray) {
        auto fit = builtinsMap.find(ki->name);
        if (fit == builtinsMap.end()) {
            continue;
        }
        ki->builtinDispatchBuilder = fit->second;
    }
    return ret;
}

void Program::extractInternalOptions(const std::string &options) {
    auto tokenized = CompilerOptions::tokenize(options);
    for (auto &optionString : internalOptionsToExtract) {
        auto element = std::find(tokenized.begin(), tokenized.end(), optionString);
        if (element == tokenized.end()) {
            continue;
        }

        if (isFlagOption(optionString)) {
            CompilerOptions::concatenateAppend(internalOptions, optionString);
        } else if ((element + 1 != tokenized.end()) &&
                   isOptionValueValid(optionString, *(element + 1))) {
            CompilerOptions::concatenateAppend(internalOptions, optionString);
            CompilerOptions::concatenateAppend(internalOptions, *(element + 1));
        }
    }
}

} // namespace NEO
