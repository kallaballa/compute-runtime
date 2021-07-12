/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/gmm_helper/gmm_interface.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/hw_info_config.h"
#include "shared/source/utilities/debug_settings_reader.h"
#include "shared/test/common/helpers/default_hw_info.inl"
#include "shared/test/common/helpers/memory_leak_listener.h"
#include "shared/test/common/helpers/test_files.h"
#include "shared/test/common/helpers/ult_hw_config.inl"
#include "shared/test/common/mocks/mock_gmm_client_context.h"
#include "shared/test/common/mocks/mock_sip.h"
#include "shared/test/unit_test/base_ult_config_listener.h"

#include "opencl/source/program/kernel_info.h"
#include "opencl/source/utilities/logger.h"
#include "opencl/test/unit_test/custom_event_listener.h"
#include "opencl/test/unit_test/global_environment.h"

#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/compiler_interface/l0_reg_path.h"

#include "gmock/gmock.h"
#include "igfxfmid.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

#ifdef WIN32
const char *fSeparator = "\\";
#else
const char *fSeparator = "/";
#endif

TEST(Should, pass) { EXPECT_TRUE(true); }

namespace L0 {

namespace ult {
TestEnvironment *environment = nullptr;
}
} // namespace L0

using namespace L0::ult;

PRODUCT_FAMILY productFamily = NEO::DEFAULT_TEST_PLATFORM::hwInfo.platform.eProductFamily;
GFXCORE_FAMILY renderCoreFamily = NEO::DEFAULT_TEST_PLATFORM::hwInfo.platform.eRenderCoreFamily;
int32_t revId = -1;
uint32_t euPerSubSlice = 0;
uint32_t sliceCount = 0;
uint32_t subSlicePerSliceCount = 0;
int dieRecovery = 0;

namespace NEO {
extern const HardwareInfo *hardwareInfoTable[IGFX_MAX_PRODUCT];
extern bool useMockGmm;
extern TestMode testMode;
extern const char *executionDirectorySuffix;

namespace MockSipData {
extern std::unique_ptr<MockSipKernel> mockSipKernel;
}
} // namespace NEO

std::string getRunPath(char *argv0) {
    std::string res(argv0);

    auto pos = res.rfind(fSeparator);
    if (pos != std::string::npos)
        res = res.substr(0, pos);

    if (res == "." || pos == std::string::npos) {
        char *cwd;
#if defined(__linux__)
        cwd = getcwd(nullptr, 0);
#else
        cwd = _getcwd(nullptr, 0);
#endif
        res = cwd;
        free(cwd);
    }

    return res;
}

std::thread::id tempThreadID;

void applyWorkarounds() {
    {
        std::ofstream f;
        const std::string fileName("_tmp_");
        f.open(fileName, std::ofstream::binary);
        f.close();
    }
    {
        std::mutex mtx;
        std::unique_lock<std::mutex> stateLock(mtx);
    }
    {
        std::stringstream ss("1");
        int val;
        ss >> val;
    }
    {
        class BaseClass {
          public:
            int method(int param) { return 1; }
        };
        class MockClass : public BaseClass {
          public:
            MOCK_METHOD1(method, int(int param));
        };
        ::testing::NiceMock<MockClass> mockObj;
        EXPECT_CALL(mockObj, method(::testing::_))
            .Times(1);
        mockObj.method(2);
    }

    //intialize rand
    srand(static_cast<unsigned int>(time(nullptr)));

    //Create at least on thread to prevent false memory leaks in tests using threads
    std::thread t([&]() {
    });
    tempThreadID = t.get_id();
    t.join();

    //Create FileLogger to prevent false memory leaks
    {
        NEO::FileLoggerInstance();
    }
}

int main(int argc, char **argv) {
    bool useDefaultListener = false;
    bool setupFeatureTableAndWorkaroundTable = testMode == TestMode::AubTests ? true : false;
    applyWorkarounds();

    testing::InitGoogleMock(&argc, argv);

    NEO::HardwareInfo hwInfoForTests = NEO::DEFAULT_TEST_PLATFORM::hwInfo;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp("--product", argv[i])) {
            ++i;
            if (i < argc) {
                if (::isdigit(argv[i][0])) {
                    int productValue = atoi(argv[i]);
                    if (productValue > 0 && productValue < IGFX_MAX_PRODUCT &&
                        NEO::hardwarePrefix[productValue] != nullptr) {
                        productFamily = static_cast<PRODUCT_FAMILY>(productValue);
                    } else {
                        productFamily = IGFX_UNKNOWN;
                    }
                } else {
                    productFamily = IGFX_UNKNOWN;
                    for (int j = 0; j < IGFX_MAX_PRODUCT; j++) {
                        if (NEO::hardwarePrefix[j] == nullptr)
                            continue;
                        if (strcmp(NEO::hardwarePrefix[j], argv[i]) == 0) {
                            productFamily = static_cast<PRODUCT_FAMILY>(j);
                            break;
                        }
                    }
                }
                if (productFamily == IGFX_UNKNOWN) {
                    std::cout << "unknown product family has been set: " << argv[i]
                              << std::endl;
                    return -1;
                }

                hwInfoForTests = *NEO::hardwareInfoTable[productFamily];
                if (!hwInfoForTests.capabilityTable.levelZeroSupported) {
                    std::cout << "unsupported product family has been set: " << argv[i]
                              << std::endl;
                    return 0;
                }

                std::cout << "product family: " << NEO::hardwarePrefix[productFamily] << " ("
                          << productFamily << ")" << std::endl;
            }
        } else if (!strcmp("--rev_id", argv[i])) {
            ++i;
            if (i < argc) {
                revId = atoi(argv[i]);
            }
        } else if (!strcmp("--slices", argv[i])) {
            ++i;
            if (i < argc) {
                sliceCount = atoi(argv[i]);
            }
        } else if (!strcmp("--subslices", argv[i])) {
            ++i;
            if (i < argc) {
                subSlicePerSliceCount = atoi(argv[i]);
            }
        } else if (!strcmp("--eu_per_ss", argv[i])) {
            ++i;
            if (i < argc) {
                euPerSubSlice = atoi(argv[i]);
            }
        } else if (!strcmp("--die_recovery", argv[i])) {
            ++i;
            if (i < argc) {
                dieRecovery = atoi(argv[i]) ? 1 : 0;
            }
        } else if (!strcmp("--disable_default_listener", argv[i])) {
            useDefaultListener = false;
        } else if (!strcmp("--tbx", argv[i])) {
            if (testMode == TestMode::AubTests) {
                testMode = TestMode::AubTestsWithTbx;
            }
            initialHardwareTag = 0;
        } else if (!strcmp("--read-config", argv[i]) && (testMode == TestMode::AubTests || testMode == TestMode::AubTestsWithTbx)) {
            if (DebugManager.registryReadAvailable()) {
                DebugManager.setReaderImpl(NEO::SettingsReader::create(L0::registryPath));
                DebugManager.injectSettingsFromReader();
            }
        } else if (!strcmp("--enable_default_listener", argv[i])) {
            useDefaultListener = true;
        }
    }
    productFamily = hwInfoForTests.platform.eProductFamily;
    renderCoreFamily = hwInfoForTests.platform.eRenderCoreFamily;
    if (revId == -1) {
        revId = hwInfoForTests.platform.usRevId;
    }
    sliceCount = sliceCount > 0 ? sliceCount : hwInfoForTests.gtSystemInfo.SliceCount;
    subSlicePerSliceCount = subSlicePerSliceCount > 0 ? subSlicePerSliceCount : (hwInfoForTests.gtSystemInfo.SubSliceCount / sliceCount);
    euPerSubSlice = euPerSubSlice > 0 ? euPerSubSlice : hwInfoForTests.gtSystemInfo.MaxEuPerSubSlice;

    hwInfoForTests.platform.usRevId = revId;
    hwInfoForTests.gtSystemInfo.SliceCount = sliceCount;
    hwInfoForTests.gtSystemInfo.SubSliceCount = hwInfoForTests.gtSystemInfo.SliceCount * subSlicePerSliceCount;
    hwInfoForTests.gtSystemInfo.EUCount = hwInfoForTests.gtSystemInfo.SubSliceCount * euPerSubSlice - dieRecovery;

    // Platforms with uninitialized factory are not supported
    if (L0::commandListFactory[productFamily] == nullptr) {
        std::cout << "unsupported product family has been set: " << NEO::hardwarePrefix[::productFamily] << std::endl;
        std::cout << "skipping tests" << std::endl;
        return 0;
    }

    auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
    if (useDefaultListener == false) {
        auto defaultListener = listeners.default_result_printer();

        auto customEventListener = new CCustomEventListener(defaultListener, NEO::hardwarePrefix[productFamily]);

        listeners.Release(defaultListener);
        listeners.Append(customEventListener);
    }

    listeners.Append(new NEO::MemoryLeakListener);
    listeners.Append(new NEO::BaseUltConfigListener);

    binaryNameSuffix.append(NEO::familyName[hwInfoForTests.platform.eRenderCoreFamily]);
    binaryNameSuffix.append(hwInfoForTests.capabilityTable.platformType);

    std::string testBinaryFiles = getRunPath(argv[0]);
    std::string testBinaryFilesNoRev = testBinaryFiles;
    testBinaryFilesNoRev.append("/level_zero/");
    testBinaryFiles.append("/" + binaryNameSuffix + "/");
    testBinaryFilesNoRev.append(binaryNameSuffix + "/");

    testBinaryFiles.append(std::to_string(revId));
    testBinaryFiles.append("/");
    testBinaryFiles.append(testFiles);
    testBinaryFilesNoRev.append(testFiles);
    testFiles = testBinaryFiles;
    testFilesNoRev = testBinaryFilesNoRev;

    std::string executionDirectory(hardwarePrefix[productFamily]);
    executionDirectory += NEO::executionDirectorySuffix; //_aub for aub_tests, empty otherwise
    executionDirectory += "/";
    executionDirectory += std::to_string(revId);

#ifdef WIN32
#include <direct.h>
    if (_chdir(executionDirectory.c_str())) {
        std::cout << "chdir into " << executionDirectory << " directory failed.\nThis might cause test failures." << std::endl;
    }
#elif defined(__linux__)
#include <unistd.h>
    if (chdir(executionDirectory.c_str()) != 0) {
        std::cout << "chdir into " << executionDirectory << " directory failed.\nThis might cause test failures." << std::endl;
    }
#endif

    if (useMockGmm) {
        NEO::GmmHelper::createGmmContextWrapperFunc = NEO::GmmClientContext::create<MockGmmClientContext>;
    } else {
        NEO::GmmInterface::initialize(nullptr, nullptr);
    }

    uint64_t hwInfoConfig = NEO::defaultHardwareInfoConfigTable[productFamily];
    NEO::setHwInfoValuesFromConfig(hwInfoConfig, hwInfoForTests);

    // set Gt and FeatureTable to initial state
    NEO::hardwareInfoSetup[productFamily](&hwInfoForTests, setupFeatureTableAndWorkaroundTable, hwInfoConfig);

    NEO::defaultHwInfo = std::make_unique<NEO::HardwareInfo>();
    *NEO::defaultHwInfo = hwInfoForTests;

    NEO::MockSipData::mockSipKernel.reset(new NEO::MockSipKernel());
    if (testMode == TestMode::AubTests || testMode == TestMode::AubTestsWithTbx) {
        MockSipData::useMockSip = false;
    }

    environment = reinterpret_cast<TestEnvironment *>(::testing::AddGlobalTestEnvironment(new TestEnvironment));

    MockCompilerDebugVars fclDebugVars;
    MockCompilerDebugVars igcDebugVars;

    environment->setDefaultDebugVars(fclDebugVars, igcDebugVars, hwInfoForTests);

    auto retVal = RUN_ALL_TESTS();

    return retVal;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
