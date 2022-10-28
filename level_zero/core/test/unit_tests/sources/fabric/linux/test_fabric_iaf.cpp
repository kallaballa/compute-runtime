/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/libult/linux/drm_mock.h"

#include "level_zero/core/source/fabric/linux/fabric_device_iaf.h"
#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"

#include "gtest/gtest.h"

namespace L0 {
namespace ult {

class MockIafNlApi : public IafNlApi {
  public:
    ze_result_t fPortStatusQueryStatus = ZE_RESULT_SUCCESS;
    ze_result_t fportPropertiesStatus = ZE_RESULT_SUCCESS;
    ze_result_t subDevicePropertiesStatus = ZE_RESULT_SUCCESS;
    ze_result_t getPortsStatus = ZE_RESULT_SUCCESS;
    iaf_fport_health fPortStatusQueryHealthStatus = IAF_FPORT_HEALTH_HEALTHY;
    bool portEnumerationEnable = true;

    ze_result_t handleResponse(const uint16_t cmdOp, struct genl_info *info, void *pOutput) override { return ZE_RESULT_SUCCESS; };
    ze_result_t fPortStatusQuery(const IafPortId portId, IafPortState &state) override {
        state.healthStatus = fPortStatusQueryHealthStatus;
        return fPortStatusQueryStatus;
    };

    ze_result_t getThroughput(const IafPortId portId, IafPortThroughPut &throughput) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portStateQuery(const IafPortId portId, bool &enabled) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portBeaconStateQuery(const IafPortId portId, bool &enabled) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portBeaconEnable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portBeaconDisable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portEnable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portDisable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portUsageEnable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t portUsageDisable(const IafPortId portId) override { return ZE_RESULT_SUCCESS; };
    ze_result_t remRequest() override { return ZE_RESULT_SUCCESS; };
    ze_result_t routingGenQuery(uint32_t &start, uint32_t &end) override { return ZE_RESULT_SUCCESS; };
    ze_result_t deviceEnum(std::vector<uint32_t> &fabricIds) override { return ZE_RESULT_SUCCESS; };
    ze_result_t fabricDeviceProperties(const uint32_t fabricId, uint32_t &numSubdevices) override { return ZE_RESULT_SUCCESS; };
    ze_result_t subdevicePropertiesGet(const uint32_t fabricId, const uint32_t attachId, uint64_t &guid, std::vector<uint8_t> &ports) override {
        return subDevicePropertiesStatus;
    };
    ze_result_t fportProperties(const IafPortId portId, uint64_t &neighborGuid, uint8_t &neighborPortNumber,
                                IafPortSpeed &maxRxSpeed, IafPortSpeed &maxTxSpeed,
                                IafPortSpeed &rxSpeed, IafPortSpeed &txSpeed) override {
        neighborPortNumber = 8;
        neighborGuid = 0xFEEDBEAD;
        maxRxSpeed.width = 4;
        maxRxSpeed.bitRate = 53125000000;
        maxTxSpeed = maxRxSpeed;
        rxSpeed = maxRxSpeed;
        txSpeed = maxRxSpeed;

        return fportPropertiesStatus;
    };
    ze_result_t getPorts(const std::string &devicePciPath, std::vector<IafPort> &ports) override {

        if (portEnumerationEnable) {
            IafPort defaultPort;
            defaultPort.onSubdevice = true;
            defaultPort.portId.fabricId = testPortId.fabricId;
            defaultPort.portId.attachId = testPortId.attachId;
            defaultPort.portId.portNumber = testPortId.portNumber;
            defaultPort.model = "XeLink";
            defaultPort.maxRxSpeed.width = 4;
            defaultPort.maxRxSpeed.bitRate = 53125000000;
            defaultPort.maxTxSpeed = defaultPort.maxRxSpeed;

            ports.push_back(defaultPort);
            defaultPort.portId.fabricId = testPortId.fabricId + 1;
            defaultPort.portId.attachId = testPortId.attachId + 1;
            defaultPort.portId.portNumber = testPortId.portNumber + 1;
            ports.push_back(defaultPort);
            defaultPort.portId.fabricId = testPortId.fabricId + 2;
            defaultPort.portId.attachId = testPortId.attachId;
            defaultPort.portId.portNumber = testPortId.portNumber + 2;
            ports.push_back(defaultPort);
        }

        return getPortsStatus;
    };

    IafPortId testPortId = {};
};

struct TestFabricIaf : public ::testing::Test {
    void SetUp() override {
        DebugManager.flags.CreateMultipleSubDevices.set(1);
        executionEnvironment = new NEO::ExecutionEnvironment();
        executionEnvironment->prepareRootDeviceEnvironments(1);
        executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(defaultHwInfo.get());
        osInterface = new OSInterface();
        drmMock = new DrmMockResources(*executionEnvironment->rootDeviceEnvironments[0]);
        executionEnvironment->rootDeviceEnvironments[0]->osInterface.reset(osInterface);
        executionEnvironment->rootDeviceEnvironments[0]->osInterface->setDriverModel(std::unique_ptr<Drm>(drmMock));
        neoDevice = NEO::MockDevice::create<NEO::MockDevice>(executionEnvironment, 0u);
    }
    void TearDown() override {}

    DebugManagerStateRestore restorer;

    NEO::ExecutionEnvironment *executionEnvironment = nullptr;
    OSInterface *osInterface = nullptr;
    NEO::MockDevice *neoDevice = nullptr;
    DrmMockResources *drmMock = nullptr;
    L0::DriverHandleImp *driverHandle = nullptr;
};

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenFabricVerticesAreCreatedThenEnumerationIsSuccessful) {

    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));

    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    subDeviceFabric->setIafNlApi(new MockIafNlApi());
    EXPECT_EQ(ZE_RESULT_SUCCESS, subDeviceFabric->enumerate());
    delete subDeviceFabric;

    FabricDeviceIaf *deviceFabric = new FabricDeviceIaf(deviceImp);
    deviceFabric->subDeviceIafs[0]->setIafNlApi(new MockIafNlApi());
    EXPECT_EQ(ZE_RESULT_SUCCESS, deviceFabric->enumerate());
    delete deviceFabric;
}

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenPortStatusQueryIsUnsuccessfulThenNoConnectionsAreEnumerated) {

    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));

    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    MockIafNlApi *mockNlApi = new MockIafNlApi();
    mockNlApi->fPortStatusQueryStatus = ZE_RESULT_ERROR_UNKNOWN;
    subDeviceFabric->setIafNlApi(mockNlApi);
    subDeviceFabric->enumerate();
    EXPECT_EQ(subDeviceFabric->connections.size(), 0u);
    delete subDeviceFabric;

    subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    mockNlApi = new MockIafNlApi();
    mockNlApi->fPortStatusQueryStatus = ZE_RESULT_SUCCESS;
    mockNlApi->fPortStatusQueryHealthStatus = IAF_FPORT_HEALTH_DEGRADED;
    subDeviceFabric->setIafNlApi(mockNlApi);
    subDeviceFabric->enumerate();
    EXPECT_EQ(subDeviceFabric->connections.size(), 0u);
    delete subDeviceFabric;
}

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenPortPropertiesQueryIsUnsuccessfulThenNoConnectionsAreEnumerated) {

    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));

    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    MockIafNlApi *mockNlApi = new MockIafNlApi();
    mockNlApi->fportPropertiesStatus = ZE_RESULT_ERROR_UNKNOWN;
    subDeviceFabric->setIafNlApi(mockNlApi);
    EXPECT_EQ(ZE_RESULT_SUCCESS, subDeviceFabric->enumerate());
    EXPECT_EQ(subDeviceFabric->connections.size(), 0u);
    delete subDeviceFabric;
}

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenSubDevicePropertiesGetIsUnsuccessfulThenReturnError) {

    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));

    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    MockIafNlApi *mockNlApi = new MockIafNlApi();
    mockNlApi->subDevicePropertiesStatus = ZE_RESULT_ERROR_UNKNOWN;
    subDeviceFabric->setIafNlApi(mockNlApi);
    EXPECT_EQ(ZE_RESULT_ERROR_UNKNOWN, subDeviceFabric->enumerate());
    delete subDeviceFabric;
}

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenNoPortsCanBeEnumeratedThenReturnSuccess) {

    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));

    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    MockIafNlApi *mockNlApi = new MockIafNlApi();
    mockNlApi->portEnumerationEnable = false;
    subDeviceFabric->setIafNlApi(mockNlApi);
    EXPECT_EQ(ZE_RESULT_SUCCESS, subDeviceFabric->enumerate());
    delete subDeviceFabric;
}

TEST_F(TestFabricIaf, GivenIafFabricAvailableWhenGetPortsReturnsErrorThenReturnError) {
    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));
    auto deviceImp = static_cast<DeviceImp *>(driverHandle->devices[0]);

    FabricSubDeviceIaf *subDeviceFabric = new FabricSubDeviceIaf(deviceImp);
    MockIafNlApi *mockNlApi = new MockIafNlApi();
    mockNlApi->getPortsStatus = ZE_RESULT_ERROR_UNKNOWN;
    subDeviceFabric->setIafNlApi(mockNlApi);
    EXPECT_EQ(ZE_RESULT_ERROR_UNKNOWN, subDeviceFabric->enumerate());
    delete subDeviceFabric;
}

} // namespace ult
} // namespace L0