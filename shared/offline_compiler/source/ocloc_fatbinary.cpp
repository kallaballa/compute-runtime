/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/offline_compiler/source/ocloc_fatbinary.h"

#include "shared/offline_compiler/source/ocloc_arg_helper.h"
#include "shared/offline_compiler/source/offline_compiler.h"
#include "shared/offline_compiler/source/utilities/safety_caller.h"
#include "shared/source/device_binary_format/ar/ar_encoder.h"
#include "shared/source/helpers/file_io.h"
#include "shared/source/helpers/hw_info.h"

#include "compiler_options.h"
#include "igfxfmid.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace NEO {

bool requestedFatBinary(const std::vector<std::string> &args, OclocArgHelper *helper) {
    for (size_t argIndex = 1; argIndex < args.size(); argIndex++) {
        const auto &currArg = args[argIndex];
        const bool hasMoreArgs = (argIndex + 1 < args.size());
        if ((ConstStringRef("-device") == currArg) && hasMoreArgs) {
            ConstStringRef deviceArg(args[argIndex + 1]);
            return deviceArg.contains("*") || deviceArg.contains("-") || deviceArg.contains(",") || helper->isGen(deviceArg.str());
        }
    }
    return false;
}

std::vector<PRODUCT_FAMILY> getAllSupportedTargetPlatforms() {
    return std::vector<PRODUCT_FAMILY>{ALL_SUPPORTED_PRODUCT_FAMILIES};
}

std::vector<ConstStringRef> toProductNames(const std::vector<PRODUCT_FAMILY> &productIds) {
    std::vector<ConstStringRef> ret;
    for (auto prodId : productIds) {
        ret.push_back(ConstStringRef(hardwarePrefix[prodId], strlen(hardwarePrefix[prodId])));
    }
    return ret;
}

PRODUCT_FAMILY asProductId(ConstStringRef product, const std::vector<PRODUCT_FAMILY> &allSupportedPlatforms) {
    for (auto family : allSupportedPlatforms) {
        if (product == hardwarePrefix[family]) {
            return family;
        }
    }
    return IGFX_UNKNOWN;
}

void appendPlatformsForGfxCore(GFXCORE_FAMILY core, const std::vector<PRODUCT_FAMILY> &allSupportedPlatforms, std::vector<PRODUCT_FAMILY> &out) {
    for (auto family : allSupportedPlatforms) {
        if (core == hardwareInfoTable[family]->platform.eRenderCoreFamily) {
            out.push_back(family);
        }
    }
}

std::vector<ConstStringRef> getTargetPlatformsForFatbinary(ConstStringRef deviceArg, OclocArgHelper *argHelper) {
    std::vector<PRODUCT_FAMILY> allSupportedPlatforms = getAllSupportedTargetPlatforms();
    if (deviceArg == "*") {
        return toProductNames(allSupportedPlatforms);
    }

    auto genArg = ConstStringRef("gen");

    std::vector<PRODUCT_FAMILY> requestedPlatforms;
    auto sets = CompilerOptions::tokenize(deviceArg, ',');
    for (auto set : sets) {
        if (set.contains("-")) {
            auto range = CompilerOptions::tokenize(deviceArg, '-');
            if (range.size() > 2) {
                argHelper->printf("Invalid range : %s - should be from-to or -to or from-\n", set.str().c_str());
                return {};
            }

            if (range.size() == 1) {
                // open range , from-max or min-to
                if (argHelper->isGen(range[0].str())) {
                    std::vector<GFXCORE_FAMILY> coreIdList;
                    auto coreId = argHelper->returnIGFXforGen(range[0].str());
                    if (coreId == 0) {
                        argHelper->printf("Unknown device : %s\n", set.str().c_str());
                        return {};
                    }
                    coreIdList.push_back(static_cast<GFXCORE_FAMILY>(coreId));
                    if ('-' == set[0]) {
                        // to
                        auto coreId = coreIdList.back();
                        unsigned int coreIt = IGFX_UNKNOWN_CORE;
                        ++coreIt;
                        while (coreIt <= static_cast<unsigned int>(coreId)) {
                            appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreIt), allSupportedPlatforms, requestedPlatforms);
                            ++coreIt;
                        }
                    } else {
                        // from
                        unsigned int coreIt = coreIdList.front();
                        while (coreIt < static_cast<unsigned int>(IGFX_MAX_CORE)) {
                            appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreIt), allSupportedPlatforms, requestedPlatforms);
                            ++coreIt;
                        }
                    }
                } else {
                    auto prodId = asProductId(range[0], allSupportedPlatforms);
                    if (IGFX_UNKNOWN == prodId) {
                        argHelper->printf("Unknown device : %s\n", range[0].str().c_str());
                        return {};
                    }
                    auto prodIt = std::find(allSupportedPlatforms.begin(), allSupportedPlatforms.end(), prodId);
                    assert(prodIt != allSupportedPlatforms.end());
                    if ('-' == set[0]) {
                        // to
                        requestedPlatforms.insert(requestedPlatforms.end(), allSupportedPlatforms.begin(), prodIt + 1);
                    } else {
                        // from
                        requestedPlatforms.insert(requestedPlatforms.end(), prodIt, allSupportedPlatforms.end());
                    }
                }
            } else {
                if (argHelper->isGen(range[0].str())) {
                    if (false == argHelper->isGen(range[1].str())) {
                        argHelper->printf("Ranges mixing platforms and gfxCores is not supported : %s - should be genFrom-genTo or platformFrom-platformTo\n", set.str().c_str());
                        return {};
                    }
                    auto coreFrom = argHelper->returnIGFXforGen(range[0].str());
                    auto coreTo = argHelper->returnIGFXforGen(range[1].str());
                    if (coreFrom == 0) {
                        argHelper->printf("Unknown device : %s\n", set.str().c_str());
                        return {};
                    }
                    if (coreTo == 0) {
                        argHelper->printf("Unknown device : %s\n", set.str().c_str());
                        return {};
                    }
                    if (static_cast<GFXCORE_FAMILY>(coreFrom) > static_cast<GFXCORE_FAMILY>(coreTo)) {
                        std::swap(coreFrom, coreTo);
                    }
                    while (coreFrom <= coreTo) {
                        appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreFrom), allSupportedPlatforms, requestedPlatforms);
                        coreFrom = static_cast<GFXCORE_FAMILY>(static_cast<unsigned int>(coreFrom) + 1);
                    }
                } else {
                    auto platformFrom = asProductId(range[0], allSupportedPlatforms);
                    auto platformTo = asProductId(range[1], allSupportedPlatforms);
                    if (IGFX_UNKNOWN == platformFrom) {
                        argHelper->printf("Unknown device : %s\n", set.str().c_str());
                        return {};
                    }
                    if (IGFX_UNKNOWN == platformTo) {
                        argHelper->printf("Unknown device : %s\n", set.str().c_str());
                        return {};
                    }
                    if (platformFrom > platformTo) {
                        std::swap(platformFrom, platformTo);
                    }

                    auto from = std::find(allSupportedPlatforms.begin(), allSupportedPlatforms.end(), platformFrom);
                    auto to = std::find(allSupportedPlatforms.begin(), allSupportedPlatforms.end(), platformTo) + 1;
                    requestedPlatforms.insert(requestedPlatforms.end(), from, to);
                }
            }
        } else if (argHelper->isGen(set.str())) {
            if (set.size() == genArg.size()) {
                argHelper->printf("Invalid gen-based device : %s - gen should be followed by a number\n", set.str().c_str());
            } else {
                auto coreId = argHelper->returnIGFXforGen(set.str());
                if (coreId == 0) {
                    argHelper->printf("Unknown device : %s\n", set.str().c_str());
                    return {};
                }
                appendPlatformsForGfxCore(static_cast<GFXCORE_FAMILY>(coreId), allSupportedPlatforms, requestedPlatforms);
            }
        } else {
            auto prodId = asProductId(set, allSupportedPlatforms);
            if (IGFX_UNKNOWN == prodId) {
                argHelper->printf("Unknown device : %s\n", set.str().c_str());
                return {};
            }
            requestedPlatforms.push_back(prodId);
        }
    }
    return toProductNames(requestedPlatforms);
}

int buildFatBinary(const std::vector<std::string> &args, OclocArgHelper *argHelper) {
    std::string pointerSizeInBits = (sizeof(void *) == 4) ? "32" : "64";
    size_t deviceArgIndex = -1;
    std::string inputFileName = "";
    std::string outputFileName = "";
    std::string outputDirectory = "";

    std::vector<std::string> argsCopy(args);
    for (size_t argIndex = 1; argIndex < args.size(); argIndex++) {
        const auto &currArg = args[argIndex];
        const bool hasMoreArgs = (argIndex + 1 < args.size());
        if ((ConstStringRef("-device") == currArg) && hasMoreArgs) {
            deviceArgIndex = argIndex + 1;
            ++argIndex;
        } else if ((CompilerOptions::arch32bit == currArg) || (ConstStringRef("-32") == currArg)) {
            pointerSizeInBits = "32";
        } else if ((CompilerOptions::arch64bit == currArg) || (ConstStringRef("-64") == currArg)) {
            pointerSizeInBits = "64";
        } else if ((ConstStringRef("-file") == currArg) && hasMoreArgs) {
            inputFileName = args[argIndex + 1];
            ++argIndex;
        } else if ((ConstStringRef("-output") == currArg) && hasMoreArgs) {
            outputFileName = args[argIndex + 1];
            ++argIndex;
        } else if ((ConstStringRef("-out_dir") == currArg) && hasMoreArgs) {
            outputDirectory = args[argIndex + 1];
            ++argIndex;
        }
    }

    std::vector<ConstStringRef> targetPlatforms;
    targetPlatforms = getTargetPlatformsForFatbinary(ConstStringRef(args[deviceArgIndex]), argHelper);
    if (targetPlatforms.empty()) {
        argHelper->printf("Failed to parse target devices from : %s\n", args[deviceArgIndex].c_str());
        return 1;
    }

    NEO::Ar::ArEncoder fatbinary(true);

    for (auto targetPlatform : targetPlatforms) {
        int retVal = 0;
        argsCopy[deviceArgIndex] = targetPlatform.str();

        std::unique_ptr<OfflineCompiler> pCompiler{OfflineCompiler::create(argsCopy.size(), argsCopy, false, retVal, argHelper)};
        if (OfflineCompiler::ErrorCode::SUCCESS != retVal) {
            argHelper->printf("Error! Couldn't create OfflineCompiler. Exiting.\n");
            return retVal;
        }

        auto stepping = pCompiler->getHardwareInfo().platform.usRevId;
        if (retVal == 0) {
            retVal = buildWithSafetyGuard(pCompiler.get());

            std::string buildLog = pCompiler->getBuildLog();
            if (buildLog.empty() == false) {
                argHelper->printf("%s\n", buildLog.c_str());
            }

            if (retVal == 0) {
                if (!pCompiler->isQuiet())
                    argHelper->printf("Build succeeded for : %s.\n", (targetPlatform.str() + "." + std::to_string(stepping)).c_str());
            } else {
                argHelper->printf("Build failed for : %s with error code: %d\n", (targetPlatform.str() + "." + std::to_string(stepping)).c_str(), retVal);
                argHelper->printf("Command was:");
                for (const auto &arg : argsCopy)
                    argHelper->printf(" %s", arg.c_str());
                argHelper->printf("\n");
            }
        }

        if (0 != retVal) {
            return retVal;
        }

        fatbinary.appendFileEntry(pointerSizeInBits + "." + targetPlatform.str() + "." + std::to_string(stepping), pCompiler->getPackedDeviceBinaryOutput());
    }

    auto fatbinaryData = fatbinary.encode();
    std::string fatbinaryFileName = outputFileName;
    if (outputFileName.empty() && (false == inputFileName.empty())) {
        fatbinaryFileName = OfflineCompiler::getFileNameTrunk(inputFileName) + ".ar";
    }
    if (false == outputDirectory.empty()) {
        fatbinaryFileName = outputDirectory + "/" + outputFileName;
    }
    argHelper->saveOutput(fatbinaryFileName, fatbinaryData.data(), fatbinaryData.size());

    return 0;
}

} // namespace NEO
