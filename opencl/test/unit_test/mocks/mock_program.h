/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/device/device.h"
#include "shared/source/helpers/hash.h"
#include "shared/source/helpers/string.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/program/kernel_info.h"
#include "opencl/source/program/program.h"

#include "gmock/gmock.h"

#include <string>

namespace NEO {

class GraphicsAllocation;

////////////////////////////////////////////////////////////////////////////////
// Program - Core implementation
////////////////////////////////////////////////////////////////////////////////
class MockProgram : public Program {
  public:
    using Program::createProgramFromBinary;
    using Program::internalOptionsToExtract;
    using Program::kernelDebugEnabled;
    using Program::linkBinary;
    using Program::separateBlockKernels;
    using Program::updateNonUniformFlag;

    using Program::applyAdditionalOptions;
    using Program::areSpecializationConstantsInitialized;
    using Program::blockKernelManager;
    using Program::buildInfos;
    using Program::context;
    using Program::createdFrom;
    using Program::debugData;
    using Program::debugDataSize;
    using Program::extractInternalOptions;
    using Program::getKernelInfo;
    using Program::irBinary;
    using Program::irBinarySize;
    using Program::isSpirV;
    using Program::options;
    using Program::packDeviceBinary;
    using Program::pDevice;
    using Program::Program;
    using Program::programBinaryType;
    using Program::sourceCode;
    using Program::specConstantsIds;
    using Program::specConstantsSizes;
    using Program::specConstantsValues;

    MockProgram(ExecutionEnvironment &executionEnvironment) : Program(executionEnvironment, nullptr, false, nullptr) {
    }

    ~MockProgram() override {
        if (contextSet)
            context = nullptr;
    }
    KernelInfo mockKernelInfo;
    void setBuildOptions(const char *buildOptions) {
        options = buildOptions != nullptr ? buildOptions : "";
    }
    std::string &getInternalOptions() { return internalOptions; };
    void setConstantSurface(GraphicsAllocation *gfxAllocation) {
        if (gfxAllocation) {
            buildInfos[gfxAllocation->getRootDeviceIndex()].constantSurface = gfxAllocation;
        } else {
            for (auto &buildInfo : buildInfos) {
                buildInfo.constantSurface = nullptr;
            }
        }
    }
    void setGlobalSurface(GraphicsAllocation *gfxAllocation) {
        if (gfxAllocation) {
            buildInfos[gfxAllocation->getRootDeviceIndex()].globalSurface = gfxAllocation;
        } else {
            for (auto &buildInfo : buildInfos) {
                buildInfo.globalSurface = nullptr;
            }
        }
    }
    void setDevice(Device *device) {
        this->pDevice = device;
    };
    std::vector<KernelInfo *> &getKernelInfoArray() {
        return kernelInfoArray;
    }
    void addKernelInfo(KernelInfo *inInfo) {
        kernelInfoArray.push_back(inInfo);
    }
    std::vector<KernelInfo *> &getParentKernelInfoArray() {
        return parentKernelInfoArray;
    }
    std::vector<KernelInfo *> &getSubgroupKernelInfoArray() {
        return subgroupKernelInfoArray;
    }
    void setContext(Context *context) {
        this->context = context;
        contextSet = true;
    }

    void setBuildStatus(cl_build_status st) { buildStatus = st; }
    void setSourceCode(const char *ptr) { sourceCode = ptr; }
    void clearOptions() { options = ""; }
    void setCreatedFromBinary(bool createdFromBin) { isCreatedFromBinary = createdFromBin; }
    void clearLog(uint32_t rootDeviceIndex) { buildInfos[rootDeviceIndex].buildLog.clear(); }

    void setIrBinary(char *ptr, bool isSpirv) {
        irBinary.reset(ptr);
        this->isSpirV = isSpirV;
    }
    void setIrBinarySize(size_t bsz, bool isSpirv) {
        irBinarySize = bsz;
        this->isSpirV = isSpirV;
    }

    std::string getCachedFileName() const;
    void setAllowNonUniform(bool allow) {
        allowNonUniform = allow;
    }

    Device *getDevicePtr();

    bool isFlagOption(ConstStringRef option) override {
        if (isFlagOptionOverride != -1) {
            return (isFlagOptionOverride > 0);
        }
        return Program::isFlagOption(option);
    }

    bool isOptionValueValid(ConstStringRef option, ConstStringRef value) override {
        if (isOptionValueValidOverride != -1) {
            return (isOptionValueValidOverride > 0);
        }
        return Program::isOptionValueValid(option, value);
    }

    cl_int rebuildProgramFromIr() {
        this->isCreatedFromBinary = false;
        this->buildStatus = CL_BUILD_NONE;
        std::unordered_map<std::string, BuiltinDispatchInfoBuilder *> builtins;
        auto &device = this->getDevice();
        return this->build(&device, this->options.c_str(), false, builtins);
    }

    bool contextSet = false;
    int isFlagOptionOverride = -1;
    int isOptionValueValidOverride = -1;
};

class GlobalMockSipProgram : public Program {
  public:
    using Program::Program;
    GlobalMockSipProgram(ExecutionEnvironment &executionEnvironment) : Program(executionEnvironment, nullptr, false, nullptr) {
    }
    cl_int processGenBinary(uint32_t rootDeviceIndex) override;
    cl_int processGenBinaryOnce(uint32_t rootDeviceIndex);
    void resetAllocationState();
    void resetAllocation(GraphicsAllocation *allocation);
    void deleteAllocation();
    GraphicsAllocation *getAllocation();
    static void initSipProgram();
    static void shutDownSipProgram();
    static GlobalMockSipProgram *sipProgram;
    static Program *getSipProgramWithCustomBinary();

  protected:
    void *sipAllocationStorage;
    static ExecutionEnvironment executionEnvironment;
};

class GMockProgram : public Program {
  public:
    using Program::Program;
    MOCK_METHOD0(appendKernelDebugOptions, bool(void));
};

} // namespace NEO
