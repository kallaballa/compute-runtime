/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/mocks/mock_elf.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"
#include "shared/test/unit_test/compiler_interface/linker_mock.h"
#include "shared/test/unit_test/device_binary_format/zebin_tests.h"

#include "opencl/source/program/kernel_info.h"
#include "test.h"

#include "level_zero/core/source/context/context.h"
#include "level_zero/core/source/kernel/kernel_imp.h"
#include "level_zero/core/source/module/module_imp.h"
#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"
#include "level_zero/core/test/unit_tests/fixtures/module_fixture.h"
#include "level_zero/core/test/unit_tests/mocks/mock_kernel.h"
#include "level_zero/core/test/unit_tests/mocks/mock_module.h"

using ::testing::Return;

namespace L0 {
namespace ult {

using ModuleTest = Test<ModuleFixture>;

HWTEST_F(ModuleTest, givenBinaryWithDebugDataWhenModuleCreatedFromNativeBinaryThenDebugDataIsStored) {
    size_t size = 0;
    std::unique_ptr<uint8_t[]> data;
    auto result = module->getDebugInfo(&size, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    data = std::make_unique<uint8_t[]>(size);
    result = module->getDebugInfo(&size, data.get());
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, data.get());
    EXPECT_NE(0u, size);
}

HWTEST_F(ModuleTest, WhenCreatingKernelThenSuccessIsReturned) {
    ze_kernel_handle_t kernelHandle;

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = kernelName.c_str();

    ze_result_t res = module->createKernel(&kernelDesc, &kernelHandle);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    Kernel::fromHandle(kernelHandle)->destroy();
}

HWTEST_F(ModuleTest, givenZeroCountWhenGettingKernelNamesThenCountIsFilled) {
    uint32_t count = 0;
    auto result = module->getKernelNames(&count, nullptr);

    auto whiteboxModule = whitebox_cast(module.get());
    EXPECT_EQ(whiteboxModule->kernelImmDatas.size(), count);

    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

HWTEST_F(ModuleTest, givenNonZeroCountWhenGettingKernelNamesThenNamesAreReturned) {
    uint32_t count = 1;
    const char *kernelNames = nullptr;
    auto result = module->getKernelNames(&count, &kernelNames);

    EXPECT_EQ(1u, count);
    EXPECT_STREQ(this->kernelName.c_str(), kernelNames);

    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

HWTEST_F(ModuleTest, givenUserModuleTypeWhenCreatingModuleThenCorrectTypeIsSet) {
    WhiteBox<Module> module(device, nullptr, ModuleType::User);
    EXPECT_EQ(ModuleType::User, module.type);
}

HWTEST_F(ModuleTest, givenBuiltinModuleTypeWhenCreatingModuleThenCorrectTypeIsSet) {
    WhiteBox<Module> module(device, nullptr, ModuleType::Builtin);
    EXPECT_EQ(ModuleType::Builtin, module.type);
}

HWTEST_F(ModuleTest, givenUserModuleWhenCreatedThenCorrectAllocationTypeIsUsedForIsa) {
    createKernel();
    EXPECT_EQ(NEO::GraphicsAllocation::AllocationType::KERNEL_ISA, kernel->getIsaAllocation()->getAllocationType());
}

HWTEST_F(ModuleTest, givenBuiltinModuleWhenCreatedThenCorrectAllocationTypeIsUsedForIsa) {
    createModuleFromBinary(ModuleType::Builtin);
    createKernel();
    EXPECT_EQ(NEO::GraphicsAllocation::AllocationType::KERNEL_ISA_INTERNAL, kernel->getIsaAllocation()->getAllocationType());
}

using ModuleTestSupport = IsWithinProducts<IGFX_SKYLAKE, IGFX_TIGERLAKE_LP>;

HWTEST2_F(ModuleTest, givenNonPatchedTokenThenSurfaceBaseAddressIsCorrectlySet, ModuleTestSupport) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;
    ze_kernel_handle_t kernelHandle;

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = kernelName.c_str();

    ze_result_t res = module->createKernel(&kernelDesc, &kernelHandle);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto kernelImp = reinterpret_cast<L0::KernelImp *>(L0::Kernel::fromHandle(kernelHandle));

    void *devicePtr = nullptr;
    ze_device_mem_alloc_desc_t deviceDesc = {};
    res = device->getDriverHandle()->allocDeviceMem(device->toHandle(),
                                                    &deviceDesc,
                                                    16384u,
                                                    0u,
                                                    &devicePtr);
    ASSERT_EQ(ZE_RESULT_SUCCESS, res);

    auto gpuAlloc = device->getDriverHandle()->getSvmAllocsManager()->getSVMAllocs()->get(devicePtr)->gpuAllocations.getGraphicsAllocation(device->getRootDeviceIndex());
    ASSERT_NE(nullptr, gpuAlloc);

    uint32_t argIndex = 0u;
    kernelImp->setBufferSurfaceState(argIndex, devicePtr, gpuAlloc);

    auto argInfo = kernelImp->getImmutableData()->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescPointer>();
    auto surfaceStateAddressRaw = ptrOffset(kernelImp->getSurfaceStateHeapData(), argInfo.bindful);
    auto surfaceStateAddress = reinterpret_cast<RENDER_SURFACE_STATE *>(const_cast<unsigned char *>(surfaceStateAddressRaw));
    EXPECT_EQ(devicePtr, reinterpret_cast<void *>(surfaceStateAddress->getSurfaceBaseAddress()));
    EXPECT_EQ(RENDER_SURFACE_STATE::COHERENCY_TYPE_GPU_COHERENT, surfaceStateAddress->getCoherencyType());

    Kernel::fromHandle(kernelHandle)->destroy();

    device->getDriverHandle()->freeMem(devicePtr);
}

using ModuleUncachedBufferTest = Test<ModuleFixture>;

HWTEST2_F(ModuleUncachedBufferTest, givenKernelWithNonUncachedArgumentThenUncachedMocsNotRequired, ModuleTestSupport) {
    ze_kernel_handle_t kernelHandle;

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = kernelName.c_str();

    ze_result_t res = module->createKernel(&kernelDesc, &kernelHandle);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto kernelImp = reinterpret_cast<L0::KernelImp *>(L0::Kernel::fromHandle(kernelHandle));

    void *devicePtr = nullptr;
    ze_device_mem_alloc_desc_t deviceDesc = {};
    res = device->getDriverHandle()->allocDeviceMem(device->toHandle(),
                                                    &deviceDesc,
                                                    16384u,
                                                    0u,
                                                    &devicePtr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto gpuAlloc = device->getDriverHandle()->getSvmAllocsManager()->getSVMAllocs()->get(devicePtr)->gpuAllocations.getGraphicsAllocation(device->getRootDeviceIndex());
    EXPECT_NE(nullptr, gpuAlloc);

    uint32_t argIndex = 0u;
    kernelImp->setArgBufferWithAlloc(argIndex, reinterpret_cast<uintptr_t>(devicePtr), gpuAlloc);
    EXPECT_FALSE(kernelImp->getKernelRequiresUncachedMocs());

    Kernel::fromHandle(kernelHandle)->destroy();

    device->getDriverHandle()->freeMem(devicePtr);
}

HWTEST2_F(ModuleUncachedBufferTest, givenKernelWithUncachedArgumentThenCorrectMocsAreSet, ModuleTestSupport) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;
    ze_kernel_handle_t kernelHandle;

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = kernelName.c_str();

    ze_result_t res = module->createKernel(&kernelDesc, &kernelHandle);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto kernelImp = reinterpret_cast<L0::KernelImp *>(L0::Kernel::fromHandle(kernelHandle));

    void *devicePtr = nullptr;
    ze_device_mem_alloc_desc_t deviceDesc = {};
    deviceDesc.flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_UNCACHED;
    res = device->getDriverHandle()->allocDeviceMem(device->toHandle(),
                                                    &deviceDesc,
                                                    16384u,
                                                    0u,
                                                    &devicePtr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto gpuAlloc = device->getDriverHandle()->getSvmAllocsManager()->getSVMAllocs()->get(devicePtr)->gpuAllocations.getGraphicsAllocation(device->getRootDeviceIndex());
    EXPECT_NE(nullptr, gpuAlloc);

    uint32_t argIndex = 0u;
    kernelImp->setArgBufferWithAlloc(argIndex, reinterpret_cast<uintptr_t>(devicePtr), gpuAlloc);
    EXPECT_FALSE(kernelImp->getKernelRequiresUncachedMocs());

    auto argInfo = kernelImp->getImmutableData()->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescPointer>();
    auto surfaceStateAddressRaw = ptrOffset(kernelImp->getSurfaceStateHeapData(), argInfo.bindful);
    auto surfaceStateAddress = reinterpret_cast<RENDER_SURFACE_STATE *>(const_cast<unsigned char *>(surfaceStateAddressRaw));
    EXPECT_EQ(devicePtr, reinterpret_cast<void *>(surfaceStateAddress->getSurfaceBaseAddress()));

    auto gmmHelper = device->getNEODevice()->getGmmHelper();
    uint32_t expectedMocs = gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CACHELINE_MISALIGNED);

    EXPECT_EQ(expectedMocs, surfaceStateAddress->getMemoryObjectControlStateReserved());

    Kernel::fromHandle(kernelHandle)->destroy();

    device->getDriverHandle()->freeMem(devicePtr);
}

HWTEST_F(ModuleTest, GivenIncorrectNameWhenCreatingKernelThenResultErrorInvalidArgumentErrorIsReturned) {
    ze_kernel_handle_t kernelHandle;

    ze_kernel_desc_t kernelDesc = {};
    kernelDesc.pKernelName = "nonexistent_function";

    ze_result_t res = module->createKernel(&kernelDesc, &kernelHandle);

    EXPECT_EQ(ZE_RESULT_ERROR_INVALID_ARGUMENT, res);
}
template <typename T1, typename T2>
struct ModuleSpecConstantsTests : public DeviceFixture,
                                  public ::testing::Test {
    void SetUp() override {
        DeviceFixture::SetUp();

        mockCompiler = new MockCompilerInterfaceWithSpecConstants<T1, T2>(moduleNumSpecConstants);
        auto rootDeviceEnvironment = neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[0].get();
        rootDeviceEnvironment->compilerInterface.reset(mockCompiler);

        mockTranslationUnit = new MockModuleTranslationUnit(device);
    }

    void TearDown() override {
        DeviceFixture::TearDown();
    }

    void runTest() {
        std::string testFile;
        retrieveBinaryKernelFilenameNoRevision(testFile, binaryFilename + "_", ".spv");

        size_t size = 0;
        auto src = loadDataFromFile(testFile.c_str(), size);

        ASSERT_NE(0u, size);
        ASSERT_NE(nullptr, src);

        ze_module_desc_t moduleDesc = {};
        moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
        moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
        moduleDesc.inputSize = size;

        specConstants.numConstants = mockCompiler->moduleNumSpecConstants;
        for (uint32_t i = mockCompiler->moduleNumSpecConstants / 2; i > 0; i--) {
            specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT1[i - 1]);
            specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT2[i - 1]);
        }
        for (uint32_t i = mockCompiler->moduleNumSpecConstants; i > 0; i--) {
            specConstantsPointerIds.push_back(mockCompiler->moduleSpecConstantsIds[i - 1]);
        }
        specConstants.pConstantIds = specConstantsPointerIds.data();
        specConstants.pConstantValues = specConstantsPointerValues.data();
        moduleDesc.pConstants = &specConstants;

        auto module = new Module(device, nullptr, ModuleType::User);
        module->translationUnit.reset(mockTranslationUnit);

        bool success = module->initialize(&moduleDesc, neoDevice);
        for (uint32_t i = 0; i < mockCompiler->moduleNumSpecConstants / 2; i++) {
            EXPECT_EQ(static_cast<uint64_t>(module->translationUnit->specConstantsValues[mockCompiler->moduleSpecConstantsIds[2 * i]]), static_cast<uint64_t>(mockCompiler->moduleSpecConstantsValuesT2[i]));
            EXPECT_EQ(static_cast<uint64_t>(module->translationUnit->specConstantsValues[mockCompiler->moduleSpecConstantsIds[2 * i + 1]]), static_cast<uint64_t>(mockCompiler->moduleSpecConstantsValuesT1[i]));
        }
        EXPECT_TRUE(success);
        module->destroy();
    }

    const uint32_t moduleNumSpecConstants = 3 * 2;
    ze_module_constants_t specConstants;
    std::vector<const void *> specConstantsPointerValues;
    std::vector<uint32_t> specConstantsPointerIds;

    const std::string binaryFilename = "test_kernel";
    const std::string kernelName = "test";
    MockCompilerInterfaceWithSpecConstants<T1, T2> *mockCompiler;
    MockModuleTranslationUnit *mockTranslationUnit;
};

using ModuleSpecConstantsLongTests = ModuleSpecConstantsTests<uint32_t, uint64_t>;
TEST_F(ModuleSpecConstantsLongTests, givenSpecializationConstantsSetWithLongSizeInDescriptorThenModuleCorrectlyPassesThemToTheCompiler) {
    runTest();
}
using ModuleSpecConstantsCharTests = ModuleSpecConstantsTests<char, uint32_t>;
TEST_F(ModuleSpecConstantsCharTests, givenSpecializationConstantsSetWithCharSizeInDescriptorThenModuleCorrectlyPassesThemToTheCompiler) {
    runTest();
}

TEST_F(ModuleSpecConstantsLongTests, givenSpecializationConstantsSetWhenCompilerReturnsErrorThenModuleInitFails) {
    class FailingMockCompilerInterfaceWithSpecConstants : public MockCompilerInterfaceWithSpecConstants<uint32_t, uint64_t> {
      public:
        FailingMockCompilerInterfaceWithSpecConstants(uint32_t moduleNumSpecConstants) : MockCompilerInterfaceWithSpecConstants<uint32_t, uint64_t>(moduleNumSpecConstants) {}
        NEO::TranslationOutput::ErrorCode getSpecConstantsInfo(const NEO::Device &device,
                                                               ArrayRef<const char> srcSpirV, NEO::SpecConstantInfo &output) override {
            return NEO::TranslationOutput::ErrorCode::CompilerNotAvailable;
        }
    };
    mockCompiler = new FailingMockCompilerInterfaceWithSpecConstants(moduleNumSpecConstants);
    auto rootDeviceEnvironment = neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[0].get();
    rootDeviceEnvironment->compilerInterface.reset(mockCompiler);
    std::string testFile;
    retrieveBinaryKernelFilenameNoRevision(testFile, binaryFilename + "_", ".spv");

    size_t size = 0;
    auto src = loadDataFromFile(testFile.c_str(), size);

    ASSERT_NE(0u, size);
    ASSERT_NE(nullptr, src);

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
    moduleDesc.inputSize = size;

    specConstants.numConstants = mockCompiler->moduleNumSpecConstants;
    for (uint32_t i = 0; i < mockCompiler->moduleNumSpecConstants / 2; i++) {
        specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT2[i]);
        specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT1[i]);
    }

    specConstants.pConstantIds = mockCompiler->moduleSpecConstantsIds.data();
    specConstants.pConstantValues = specConstantsPointerValues.data();
    moduleDesc.pConstants = &specConstants;

    auto module = new Module(device, nullptr, ModuleType::User);
    module->translationUnit.reset(mockTranslationUnit);

    bool success = module->initialize(&moduleDesc, neoDevice);
    EXPECT_FALSE(success);
    module->destroy();
}

TEST_F(ModuleSpecConstantsLongTests, givenSpecializationConstantsSetWhenUserPassTooMuchConstsIdsThenModuleInitFails) {
    std::string testFile;
    retrieveBinaryKernelFilenameNoRevision(testFile, binaryFilename + "_", ".spv");

    size_t size = 0;
    auto src = loadDataFromFile(testFile.c_str(), size);

    ASSERT_NE(0u, size);
    ASSERT_NE(nullptr, src);

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
    moduleDesc.inputSize = size;

    specConstants.numConstants = mockCompiler->moduleNumSpecConstants;
    for (uint32_t i = mockCompiler->moduleNumSpecConstants / 2; i > 0; i--) {
        specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT1[i - 1]);
        specConstantsPointerValues.push_back(&mockCompiler->moduleSpecConstantsValuesT2[i - 1]);
    }
    for (uint32_t i = mockCompiler->moduleNumSpecConstants; i > 0; i--) {
        specConstantsPointerIds.push_back(mockCompiler->moduleSpecConstantsIds[i - 1]);
    }
    specConstantsPointerIds.push_back(0x1000);
    specConstants.numConstants += 1;

    specConstants.pConstantIds = specConstantsPointerIds.data();
    specConstants.pConstantValues = specConstantsPointerValues.data();
    moduleDesc.pConstants = &specConstants;

    auto module = new Module(device, nullptr, ModuleType::User);
    module->translationUnit.reset(mockTranslationUnit);

    bool success = module->initialize(&moduleDesc, neoDevice);
    EXPECT_FALSE(success);
    module->destroy();
}

using ModuleLinkingTest = Test<DeviceFixture>;

HWTEST_F(ModuleLinkingTest, whenExternFunctionsAllocationIsPresentThenItsBeingAddedToResidencyContainer) {
    Mock<Module> module(device, nullptr);
    MockGraphicsAllocation alloc;
    module.exportedFunctionsSurface = &alloc;
    module.kernelImmDatas.push_back(std::make_unique<L0::KernelImmutableData>());
    module.translationUnit->programInfo.linkerInput.reset(new NEO::LinkerInput);
    module.linkBinary();
    ASSERT_EQ(1U, module.kernelImmDatas[0]->getResidencyContainer().size());
    EXPECT_EQ(&alloc, module.kernelImmDatas[0]->getResidencyContainer()[0]);
}

HWTEST_F(ModuleLinkingTest, givenFailureDuringLinkingWhenCreatingModuleThenModuleInitialiationFails) {
    auto mockCompiler = new MockCompilerInterface();
    auto rootDeviceEnvironment = neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[0].get();
    rootDeviceEnvironment->compilerInterface.reset(mockCompiler);

    auto mockTranslationUnit = new MockModuleTranslationUnit(device);

    auto linkerInput = std::make_unique<::WhiteBox<NEO::LinkerInput>>();
    linkerInput->valid = false;

    mockTranslationUnit->programInfo.linkerInput = std::move(linkerInput);
    uint8_t spirvData{};

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = &spirvData;
    moduleDesc.inputSize = sizeof(spirvData);

    Module module(device, nullptr, ModuleType::User);
    module.translationUnit.reset(mockTranslationUnit);

    bool success = module.initialize(&moduleDesc, neoDevice);
    EXPECT_FALSE(success);
}

HWTEST_F(ModuleLinkingTest, givenRemainingUnresolvedSymbolsDuringLinkingWhenCreatingModuleThenModuleIsNotLinkedFully) {
    auto mockCompiler = new MockCompilerInterface();
    auto rootDeviceEnvironment = neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[0].get();
    rootDeviceEnvironment->compilerInterface.reset(mockCompiler);

    auto mockTranslationUnit = new MockModuleTranslationUnit(device);

    auto linkerInput = std::make_unique<::WhiteBox<NEO::LinkerInput>>();

    NEO::LinkerInput::RelocationInfo relocation;
    relocation.symbolName = "unresolved";
    linkerInput->dataRelocations.push_back(relocation);
    linkerInput->traits.requiresPatchingOfGlobalVariablesBuffer = true;

    mockTranslationUnit->programInfo.linkerInput = std::move(linkerInput);
    uint8_t spirvData{};

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = &spirvData;
    moduleDesc.inputSize = sizeof(spirvData);

    Module module(device, nullptr, ModuleType::User);
    module.translationUnit.reset(mockTranslationUnit);

    bool success = module.initialize(&moduleDesc, neoDevice);
    EXPECT_TRUE(success);
    EXPECT_FALSE(module.isFullyLinked);
}
HWTEST_F(ModuleLinkingTest, givenNotFullyLinkedModuleWhenCreatingKernelThenErrorIsReturned) {
    Module module(device, nullptr, ModuleType::User);
    module.isFullyLinked = false;

    auto retVal = module.createKernel(nullptr, nullptr);

    EXPECT_EQ(ZE_RESULT_ERROR_MODULE_BUILD_FAILURE, retVal);
}

using ModulePropertyTest = Test<ModuleFixture>;

TEST_F(ModulePropertyTest, whenZeModuleGetPropertiesIsCalledThenGetPropertiesIsCalled) {
    Mock<Module> module(device, nullptr);
    ze_module_properties_t moduleProperties;
    moduleProperties.stype = ZE_STRUCTURE_TYPE_MODULE_PROPERTIES;
    moduleProperties.pNext = nullptr;

    // returning error code that is unlikely to be returned by the function
    EXPECT_CALL(module, getProperties(&moduleProperties))
        .Times(1)
        .WillRepeatedly(Return(ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT));

    ze_result_t res = zeModuleGetProperties(module.toHandle(), &moduleProperties);
    EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT, res);
}

TEST_F(ModulePropertyTest, givenCallToGetPropertiesWithoutUnresolvedSymbolsThenFlagIsNotSet) {
    ze_module_properties_t moduleProperties;

    ze_module_property_flags_t expectedFlags = 0;

    ze_result_t res = module->getProperties(&moduleProperties);
    moduleProperties.stype = ZE_STRUCTURE_TYPE_MODULE_PROPERTIES;
    moduleProperties.pNext = nullptr;

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(expectedFlags, moduleProperties.flags);
}

TEST_F(ModulePropertyTest, givenCallToGetPropertiesWithUnresolvedSymbolsThenFlagIsSet) {
    NEO::Linker::RelocationInfo unresolvedRelocation;
    unresolvedRelocation.symbolName = "unresolved";

    whitebox_cast(module.get())->unresolvedExternalsInfo.push_back({unresolvedRelocation});

    ze_module_property_flags_t expectedFlags = 0;
    expectedFlags |= ZE_MODULE_PROPERTY_FLAG_IMPORTS;

    ze_module_properties_t moduleProperties;
    moduleProperties.stype = ZE_STRUCTURE_TYPE_MODULE_PROPERTIES;
    moduleProperties.pNext = nullptr;

    ze_result_t res = module->getProperties(&moduleProperties);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(expectedFlags, moduleProperties.flags);
}

struct ModuleDynamicLinkTests : public Test<ModuleFixture> {
    void SetUp() override {
        Test<ModuleFixture>::SetUp();
        module0 = std::make_unique<Module>(device, nullptr, ModuleType::User);
        module1 = std::make_unique<Module>(device, nullptr, ModuleType::User);
    }
    std::unique_ptr<Module> module0;
    std::unique_ptr<Module> module1;
};

TEST_F(ModuleDynamicLinkTests, givenCallToDynamicLinkOnModulesWithoutUnresolvedSymbolsThenSuccessIsReturned) {
    std::vector<ze_module_handle_t> hModules = {module0->toHandle(), module1->toHandle()};
    ze_result_t res = module0->performDynamicLink(2, hModules.data(), nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
}

TEST_F(ModuleDynamicLinkTests, givenModuleWithUnresolvedSymbolsNotPresentInOtherModulesWhenDynamicLinkThenLinkFailureIsReturned) {

    NEO::Linker::RelocationInfo unresolvedRelocation;
    unresolvedRelocation.symbolName = "unresolved";

    module0->unresolvedExternalsInfo.push_back({unresolvedRelocation});

    std::vector<ze_module_handle_t> hModules = {module0->toHandle(), module1->toHandle()};
    ze_result_t res = module0->performDynamicLink(2, hModules.data(), nullptr);
    EXPECT_EQ(ZE_RESULT_ERROR_MODULE_LINK_FAILURE, res);
}
TEST_F(ModuleDynamicLinkTests, whenModuleIsAlreadyLinkedThenThereIsNoSymbolsVerification) {

    NEO::Linker::RelocationInfo unresolvedRelocation;
    unresolvedRelocation.symbolName = "unresolved";

    module0->unresolvedExternalsInfo.push_back({unresolvedRelocation});
    module0->isFullyLinked = true;

    std::vector<ze_module_handle_t> hModules = {module0->toHandle(), module1->toHandle()};
    ze_result_t res = module0->performDynamicLink(2, hModules.data(), nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
}

TEST_F(ModuleDynamicLinkTests, givenModuleWithUnresolvedSymbolWhenTheOtherModuleDefinesTheSymbolThenTheSegmentIsPatched) {

    uint64_t gpuAddress = 0x12345;
    uint32_t offset = 0x20;

    NEO::Linker::RelocationInfo unresolvedRelocation;
    unresolvedRelocation.symbolName = "unresolved";
    unresolvedRelocation.offset = offset;
    unresolvedRelocation.type = NEO::Linker::RelocationInfo::Type::Address;
    NEO::Linker::UnresolvedExternal unresolvedExternal;
    unresolvedExternal.unresolvedRelocation = unresolvedRelocation;

    NEO::SymbolInfo symbolInfo{};
    NEO::Linker::RelocatedSymbol relocatedSymbol{symbolInfo, gpuAddress};

    char kernelHeap[MemoryConstants::pageSize] = {};

    auto kernelInfo = std::make_unique<NEO::KernelInfo>();
    kernelInfo->heapInfo.pKernelHeap = kernelHeap;
    kernelInfo->heapInfo.KernelHeapSize = MemoryConstants::pageSize;
    module0->getTranslationUnit()->programInfo.kernelInfos.push_back(kernelInfo.release());

    auto linkerInput = std::make_unique<::WhiteBox<NEO::LinkerInput>>();
    linkerInput->traits.requiresPatchingOfInstructionSegments = true;

    module0->getTranslationUnit()->programInfo.linkerInput = std::move(linkerInput);
    module0->unresolvedExternalsInfo.push_back({unresolvedRelocation});
    module0->unresolvedExternalsInfo[0].instructionsSegmentId = 0u;

    auto kernelImmData = std::make_unique<WhiteBox<::L0::KernelImmutableData>>(device);
    kernelImmData->isaGraphicsAllocation.reset(neoDevice->getMemoryManager()->allocateGraphicsMemoryWithProperties(
        {device->getRootDeviceIndex(), MemoryConstants::pageSize, NEO::GraphicsAllocation::AllocationType::KERNEL_ISA, neoDevice->getDeviceBitfield()}));

    auto isaPtr = kernelImmData->getIsaGraphicsAllocation()->getUnderlyingBuffer();

    module0->kernelImmDatas.push_back(std::move(kernelImmData));

    module1->symbols[unresolvedRelocation.symbolName] = relocatedSymbol;

    std::vector<ze_module_handle_t> hModules = {module0->toHandle(), module1->toHandle()};
    ze_result_t res = module0->performDynamicLink(2, hModules.data(), nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    EXPECT_EQ(gpuAddress, *reinterpret_cast<uint64_t *>(ptrOffset(isaPtr, offset)));
}

class DeviceModuleSetArgBufferTest : public ModuleFixture, public ::testing::Test {
  public:
    void SetUp() override {
        ModuleFixture::SetUp();
    }

    void TearDown() override {
        ModuleFixture::TearDown();
    }

    void createKernelAndAllocMemory(uint32_t rootDeviceIndex, void **ptr, ze_kernel_handle_t *kernelHandle) {
        ze_kernel_desc_t kernelDesc = {};
        kernelDesc.pKernelName = kernelName.c_str();
        ze_result_t res = module.get()->createKernel(&kernelDesc, kernelHandle);
        EXPECT_EQ(ZE_RESULT_SUCCESS, res);

        ze_host_mem_alloc_desc_t hostDesc = {};
        res = driverHandle->allocHostMem(&hostDesc, 4096u, rootDeviceIndex, ptr);
        EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    }
};

HWTEST_F(DeviceModuleSetArgBufferTest,
         givenValidMemoryUsedinFirstCallToSetArgBufferThenNullptrSetOnTheSecondCallThenArgBufferisUpdatedInEachCallAndSuccessIsReturned) {
    uint32_t rootDeviceIndex = 0;
    createModuleFromBinary();

    ze_kernel_handle_t kernelHandle;
    void *validBufferPtr = nullptr;
    createKernelAndAllocMemory(rootDeviceIndex, &validBufferPtr, &kernelHandle);

    L0::KernelImp *kernel = reinterpret_cast<L0::KernelImp *>(Kernel::fromHandle(kernelHandle));
    ze_result_t res = kernel->setArgBuffer(0, sizeof(validBufferPtr), &validBufferPtr);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    auto arg = kernel->getImmutableData()->getDescriptor().payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>();
    auto crossThreadData = kernel->getCrossThreadData();
    auto argBufferPtr = ptrOffset(crossThreadData, arg.stateless);
    auto argBufferValue = *reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(argBufferPtr));
    EXPECT_EQ(argBufferValue, reinterpret_cast<uint64_t>(validBufferPtr));

    for (auto alloc : kernel->getResidencyContainer()) {
        if (alloc && alloc->getGpuAddress() == reinterpret_cast<uint64_t>(validBufferPtr)) {
            EXPECT_EQ(rootDeviceIndex, alloc->getRootDeviceIndex());
        }
    }

    res = kernel->setArgBuffer(0, sizeof(validBufferPtr), nullptr);

    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    arg = kernel->getImmutableData()->getDescriptor().payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>();
    crossThreadData = kernel->getCrossThreadData();
    argBufferPtr = ptrOffset(crossThreadData, arg.stateless);
    argBufferValue = *reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(argBufferPtr));
    EXPECT_NE(argBufferValue, reinterpret_cast<uint64_t>(validBufferPtr));

    driverHandle->freeMem(validBufferPtr);
    Kernel::fromHandle(kernelHandle)->destroy();
}

class MultiDeviceModuleSetArgBufferTest : public MultiDeviceModuleFixture, public ::testing::Test {
  public:
    void SetUp() override {
        MultiDeviceModuleFixture::SetUp();
    }

    void TearDown() override {
        MultiDeviceModuleFixture::TearDown();
    }

    void createKernelAndAllocMemory(uint32_t rootDeviceIndex, void **ptr, ze_kernel_handle_t *kernelHandle) {
        ze_kernel_desc_t kernelDesc = {};
        kernelDesc.pKernelName = kernelName.c_str();
        ze_result_t res = modules[rootDeviceIndex].get()->createKernel(&kernelDesc, kernelHandle);
        EXPECT_EQ(ZE_RESULT_SUCCESS, res);

        ze_host_mem_alloc_desc_t hostDesc = {};
        res = driverHandle->allocHostMem(&hostDesc, 4096u, rootDeviceIndex, ptr);
        EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    }
};

HWTEST_F(MultiDeviceModuleSetArgBufferTest,
         givenCallsToSetArgBufferThenAllocationIsSetForCorrectDevice) {

    for (uint32_t rootDeviceIndex = 0; rootDeviceIndex < numRootDevices; rootDeviceIndex++) {
        createModuleFromBinary(rootDeviceIndex);

        ze_kernel_handle_t kernelHandle;
        void *ptr = nullptr;
        createKernelAndAllocMemory(rootDeviceIndex, &ptr, &kernelHandle);

        L0::KernelImp *kernel = reinterpret_cast<L0::KernelImp *>(Kernel::fromHandle(kernelHandle));
        kernel->setArgBuffer(0, sizeof(ptr), &ptr);

        for (auto alloc : kernel->getResidencyContainer()) {
            if (alloc && alloc->getGpuAddress() == reinterpret_cast<uint64_t>(ptr)) {
                EXPECT_EQ(rootDeviceIndex, alloc->getRootDeviceIndex());
            }
        }

        driverHandle->freeMem(ptr);
        Kernel::fromHandle(kernelHandle)->destroy();
    }
}

using ContextModuleCreateTest = Test<ContextFixture>;

HWTEST_F(ContextModuleCreateTest, givenCallToCreateModuleThenModuleIsReturned) {
    std::string testFile;
    retrieveBinaryKernelFilenameNoRevision(testFile, "test_kernel_", ".bin");

    size_t size = 0;
    auto src = loadDataFromFile(testFile.c_str(), size);

    ASSERT_NE(0u, size);
    ASSERT_NE(nullptr, src);

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_NATIVE;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
    moduleDesc.inputSize = size;

    ze_module_handle_t hModule;
    ze_device_handle_t hDevice = device->toHandle();
    ze_result_t res = context->createModule(hDevice, &moduleDesc, &hModule, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    L0::Module *pModule = L0::Module::fromHandle(hModule);
    res = pModule->destroy();
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
}

using ModuleTranslationUnitTest = Test<DeviceFixture>;

struct MockModuleTU : public L0::ModuleTranslationUnit {
    MockModuleTU(L0::Device *device) : L0::ModuleTranslationUnit(device) {}

    bool buildFromSpirV(const char *input, uint32_t inputSize, const char *buildOptions, const char *internalBuildOptions,
                        const ze_module_constants_t *pConstants) override {
        wasBuildFromSpirVCalled = true;
        return true;
    }

    bool createFromNativeBinary(const char *input, size_t inputSize) override {
        wasCreateFromNativeBinaryCalled = true;
        return L0::ModuleTranslationUnit::createFromNativeBinary(input, inputSize);
    }

    bool wasBuildFromSpirVCalled = false;
    bool wasCreateFromNativeBinaryCalled = false;
};

HWTEST_F(ModuleTranslationUnitTest, GivenRebuildPrecompiledKernelsFlagAndFileWithoutIntermediateCodeWhenCreatingModuleFromNativeBinaryThenModuleIsNotRecompiled) {
    DebugManagerStateRestore dgbRestorer;
    NEO::DebugManager.flags.RebuildPrecompiledKernels.set(true);

    std::string testFile;
    retrieveBinaryKernelFilenameNoRevision(testFile, "test_kernel_", ".gen");

    size_t size = 0;
    auto src = loadDataFromFile(testFile.c_str(), size);

    ASSERT_NE(0u, size);
    ASSERT_NE(nullptr, src);

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_NATIVE;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
    moduleDesc.inputSize = size;

    Module module(device, nullptr, ModuleType::User);
    MockModuleTU *tu = new MockModuleTU(device);
    module.translationUnit.reset(tu);

    bool success = module.initialize(&moduleDesc, neoDevice);
    EXPECT_TRUE(success);
    EXPECT_TRUE(tu->wasCreateFromNativeBinaryCalled);
    EXPECT_FALSE(tu->wasBuildFromSpirVCalled);
}

HWTEST_F(ModuleTranslationUnitTest, GivenRebuildPrecompiledKernelsFlagAndFileWithIntermediateCodeWhenCreatingModuleFromNativeBinaryThenModuleIsRecompiled) {
    DebugManagerStateRestore dgbRestorer;
    NEO::DebugManager.flags.RebuildPrecompiledKernels.set(true);

    std::string testFile;
    retrieveBinaryKernelFilenameNoRevision(testFile, "test_kernel_", ".bin");

    size_t size = 0;
    auto src = loadDataFromFile(testFile.c_str(), size);

    ASSERT_NE(0u, size);
    ASSERT_NE(nullptr, src);

    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_NATIVE;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(src.get());
    moduleDesc.inputSize = size;

    Module module(device, nullptr, ModuleType::User);
    MockModuleTU *tu = new MockModuleTU(device);
    module.translationUnit.reset(tu);

    bool success = module.initialize(&moduleDesc, neoDevice);
    EXPECT_TRUE(success);
    EXPECT_TRUE(tu->wasCreateFromNativeBinaryCalled);
    EXPECT_EQ(tu->irBinarySize != 0, tu->wasBuildFromSpirVCalled);
}

HWTEST_F(ModuleTranslationUnitTest, WhenCreatingFromNativeBinaryThenSetsUpRequiredTargetProductProperly) {
    ZebinTestData::ValidEmptyProgram emptyProgram;
    auto hwInfo = device->getNEODevice()->getHardwareInfo();

    emptyProgram.elfHeader->machine = hwInfo.platform.eProductFamily;
    L0::ModuleTranslationUnit moduleTuValid(this->device);
    bool success = moduleTuValid.createFromNativeBinary(reinterpret_cast<const char *>(emptyProgram.storage.data()), emptyProgram.storage.size());
    EXPECT_TRUE(success);

    emptyProgram.elfHeader->machine = hwInfo.platform.eProductFamily;
    ++emptyProgram.elfHeader->machine;
    L0::ModuleTranslationUnit moduleTuInvalid(this->device);
    success = moduleTuInvalid.createFromNativeBinary(reinterpret_cast<const char *>(emptyProgram.storage.data()), emptyProgram.storage.size());
    EXPECT_FALSE(success);
}

HWTEST_F(ModuleTranslationUnitTest, WhenCreatingFromZeBinaryThenLinkerInputIsCreated) {
    std::string validZeInfo = std::string("version :\'") + toString(zeInfoDecoderVersion) + R"===('
kernels:
    - name : some_kernel
      execution_env :
        simd_size : 8
    - name : some_other_kernel
      execution_env :
        simd_size : 32
)===";
    ZebinTestData::ValidEmptyProgram zebin;
    zebin.removeSection(NEO::Elf::SHT_ZEBIN::SHT_ZEBIN_ZEINFO, NEO::Elf::SectionsNamesZebin::zeInfo);
    zebin.appendSection(NEO::Elf::SHT_ZEBIN::SHT_ZEBIN_ZEINFO, NEO::Elf::SectionsNamesZebin::zeInfo, ArrayRef<const uint8_t>::fromAny(validZeInfo.data(), validZeInfo.size()));
    zebin.appendSection(NEO::Elf::SHT_PROGBITS, NEO::Elf::SectionsNamesZebin::textPrefix.str() + "some_kernel", {});
    zebin.appendSection(NEO::Elf::SHT_PROGBITS, NEO::Elf::SectionsNamesZebin::textPrefix.str() + "some_other_kernel", {});

    auto hwInfo = device->getNEODevice()->getHardwareInfo();
    zebin.elfHeader->machine = hwInfo.platform.eProductFamily;

    L0::ModuleTranslationUnit moduleTuValid(this->device);
    bool success = moduleTuValid.createFromNativeBinary(reinterpret_cast<const char *>(zebin.storage.data()), zebin.storage.size());
    EXPECT_TRUE(success);

    EXPECT_NE(nullptr, moduleTuValid.programInfo.linkerInput.get());
}

HWTEST_F(ModuleTranslationUnitTest, WhenBuildOptionsAreNullThenReuseExistingOptions) {
    struct MockCompilerInterface : CompilerInterface {
        TranslationOutput::ErrorCode build(const NEO::Device &device,
                                           const TranslationInput &input,
                                           TranslationOutput &output) override {
            receivedApiOptions = input.apiOptions.begin();
            return TranslationOutput::ErrorCode::BuildFailure;
        }
        std::string receivedApiOptions;
    };
    auto *pMockCompilerInterface = new MockCompilerInterface;
    auto &rootDeviceEnvironment = this->neoDevice->executionEnvironment->rootDeviceEnvironments[this->neoDevice->getRootDeviceIndex()];
    rootDeviceEnvironment->compilerInterface.reset(pMockCompilerInterface);

    L0::ModuleTranslationUnit moduleTu(this->device);
    moduleTu.options = "abcd";
    auto ret = moduleTu.buildFromSpirV("", 0U, nullptr, "", nullptr);
    EXPECT_FALSE(ret);
    EXPECT_STREQ("abcd", moduleTu.options.c_str());
    EXPECT_STREQ("abcd", pMockCompilerInterface->receivedApiOptions.c_str());
}

HWTEST_F(ModuleTranslationUnitTest, WhenBuildOptionsAreNullThenReuseExistingOptions2) {
    struct MockCompilerInterface : CompilerInterface {
        TranslationOutput::ErrorCode build(const NEO::Device &device,
                                           const TranslationInput &input,
                                           TranslationOutput &output) override {
            inputInternalOptions = input.internalOptions.begin();
            return TranslationOutput::ErrorCode::Success;
        }
        std::string inputInternalOptions;
    };
    auto pMockCompilerInterface = new MockCompilerInterface;
    auto &rootDeviceEnvironment = this->neoDevice->executionEnvironment->rootDeviceEnvironments[this->neoDevice->getRootDeviceIndex()];
    rootDeviceEnvironment->compilerInterface.reset(pMockCompilerInterface);

    DebugManagerStateRestore restorer;
    DebugManager.flags.DisableStatelessToStatefulOptimization.set(1);

    MockModuleTranslationUnit moduleTu(this->device);
    auto ret = moduleTu.buildFromSpirV("", 0U, nullptr, "", nullptr);
    EXPECT_TRUE(ret);
    EXPECT_NE(pMockCompilerInterface->inputInternalOptions.find("cl-intel-greater-than-4GB-buffer-required"), std::string::npos);
}

TEST(BuildOptions, givenNoSrcOptionNameInSrcNamesWhenMovingBuildOptionsThenFalseIsReturned) {
    std::string srcNames = NEO::CompilerOptions::concatenate(NEO::CompilerOptions::fastRelaxedMath, NEO::CompilerOptions::finiteMathOnly);
    std::string dstNames;

    auto result = moveBuildOption(dstNames, srcNames, BuildOptions::optDisable, NEO::CompilerOptions::optDisable);
    EXPECT_FALSE(result);
}

TEST(BuildOptions, givenSrcOptionNameInSrcNamesWhenMovingBuildOptionsThenOptionIsRemovedFromSrcNamesAndTranslatedOptionsStoredInDstNames) {
    std::string srcNames = NEO::CompilerOptions::concatenate(NEO::CompilerOptions::fastRelaxedMath, NEO::CompilerOptions::optDisable);
    std::string dstNames;

    auto result = moveBuildOption(dstNames, srcNames, BuildOptions::optDisable, NEO::CompilerOptions::optDisable);
    EXPECT_TRUE(result);

    EXPECT_EQ(BuildOptions::optDisable, dstNames);
    EXPECT_EQ(std::string::npos, srcNames.find(NEO::CompilerOptions::optDisable.str()));
}

TEST_F(ModuleTest, givenInternalOptionsWhenBindlessEnabledThenBindlesOptionsPassed) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(1);
    auto module = std::make_unique<ModuleImp>(device, nullptr, ModuleType::User);
    ASSERT_NE(nullptr, module);

    std::string buildOptions;
    std::string internalBuildOptions;

    module->createBuildOptions("", buildOptions, internalBuildOptions);

    EXPECT_TRUE(NEO::CompilerOptions::contains(internalBuildOptions, NEO::CompilerOptions::bindlessMode));
}

TEST_F(ModuleTest, givenInternalOptionsWhenBuildFlagsIsNullPtrAndBindlessEnabledThenBindlesOptionsPassed) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(1);
    auto module = std::make_unique<ModuleImp>(device, nullptr, ModuleType::User);
    ASSERT_NE(nullptr, module);

    std::string buildOptions;
    std::string internalBuildOptions;

    module->createBuildOptions(nullptr, buildOptions, internalBuildOptions);

    EXPECT_TRUE(NEO::CompilerOptions::contains(internalBuildOptions, NEO::CompilerOptions::bindlessMode));
}

TEST_F(ModuleTest, givenInternalOptionsWhenBindlessDisabledThenBindlesOptionsNotPassed) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(0);
    auto module = std::make_unique<ModuleImp>(device, nullptr, ModuleType::User);
    ASSERT_NE(nullptr, module);

    std::string buildOptions;
    std::string internalBuildOptions;

    module->createBuildOptions("", buildOptions, internalBuildOptions);

    EXPECT_FALSE(NEO::CompilerOptions::contains(internalBuildOptions, NEO::CompilerOptions::bindlessMode));
}

using ModuleDebugDataTest = Test<DeviceFixture>;
TEST_F(ModuleDebugDataTest, GivenDebugDataWithRelocationsWhenCreatingRelocatedDebugDataThenRelocationsAreApplied) {
    auto cip = new NEO::MockCompilerInterfaceCaptureBuildOptions();
    neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[device->getRootDeviceIndex()]->compilerInterface.reset(cip);

    uint8_t binary[10];
    ze_module_desc_t moduleDesc = {};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = binary;
    moduleDesc.inputSize = 10;
    ModuleBuildLog *moduleBuildLog = nullptr;

    std::unique_ptr<MockModule> module = std::make_unique<MockModule>(device,
                                                                      moduleBuildLog,
                                                                      ModuleType::User);
    module->translationUnit = std::make_unique<MockModuleTranslationUnit>(device);

    module->translationUnit->globalVarBuffer = neoDevice->getMemoryManager()->allocateGraphicsMemoryWithProperties(
        {device->getRootDeviceIndex(), MemoryConstants::pageSize, NEO::GraphicsAllocation::AllocationType::BUFFER, neoDevice->getDeviceBitfield()});
    module->translationUnit->globalConstBuffer = neoDevice->getMemoryManager()->allocateGraphicsMemoryWithProperties(
        {device->getRootDeviceIndex(), MemoryConstants::pageSize, NEO::GraphicsAllocation::AllocationType::BUFFER, neoDevice->getDeviceBitfield()});

    uint32_t kernelHeap = 0;
    auto kernelInfo = new KernelInfo();
    kernelInfo->heapInfo.KernelHeapSize = 1;
    kernelInfo->heapInfo.pKernelHeap = &kernelHeap;

    kernelInfo->kernelDescriptor.payloadMappings.implicitArgs.systemThreadSurfaceAddress.bindful = 0;
    kernelInfo->kernelDescriptor.external.debugData = std::make_unique<NEO::DebugData>();

    auto debugData = MockElfEncoder<>::createRelocateableDebugDataElf();

    kernelInfo->kernelDescriptor.external.debugData->vIsaSize = static_cast<uint32_t>(debugData.size());
    kernelInfo->kernelDescriptor.external.debugData->vIsa = reinterpret_cast<char *>(debugData.data());

    // pass kernelInfo ownership to programInfo
    module->translationUnit->programInfo.kernelInfos.push_back(kernelInfo);

    std::unique_ptr<WhiteBox<::L0::KernelImmutableData>> kernelImmData{new WhiteBox<::L0::KernelImmutableData>(this->device)};
    kernelImmData->initialize(kernelInfo, device, 0, module->translationUnit->globalConstBuffer, module->translationUnit->globalVarBuffer, false);
    kernelImmData->createRelocatedDebugData(module->translationUnit->globalConstBuffer, module->translationUnit->globalVarBuffer);

    module->kernelImmDatas.push_back(std::move(kernelImmData));

    EXPECT_NE(nullptr, kernelInfo->kernelDescriptor.external.relocatedDebugData);

    uint64_t *relocAddress = reinterpret_cast<uint64_t *>(kernelInfo->kernelDescriptor.external.relocatedDebugData.get() + 600);
    auto expectedValue = module->kernelImmDatas[0]->getIsaGraphicsAllocation()->getGpuAddress() + 0x1a8;
    EXPECT_EQ(expectedValue, *relocAddress);
}

} // namespace ult
} // namespace L0
