/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/os_agnostic_memory_manager.h"
#include "shared/source/os_interface/driver_info.h"
#include "shared/source/utilities/arrayref.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"

#include "opencl/source/api/api.h"
#include "opencl/source/mem_obj/image.h"
#include "opencl/source/platform/platform.h"
#include "opencl/source/sharings/d3d/cl_d3d_api.h"
#include "opencl/source/sharings/d3d/d3d_buffer.h"
#include "opencl/source/sharings/d3d/d3d_sharing.h"
#include "opencl/source/sharings/d3d/d3d_surface.h"
#include "opencl/source/sharings/d3d/d3d_texture.h"
#include "opencl/source/sharings/d3d/enable_d3d.h"
#include "opencl/test/unit_test/fixtures/d3d_test_fixture.h"
#include "opencl/test/unit_test/mocks/mock_buffer.h"
#include "opencl/test/unit_test/mocks/mock_sharing_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace NEO {
TYPED_TEST_CASE_P(D3DTests);

TYPED_TEST_P(D3DTests, GivenSpecificDeviceSetWhenGettingDeviceIDsFromD3DThenOnlySelectedDevicesAreReturned) {
    cl_device_id expectedDevice = *this->devices;
    cl_device_id device = 0;
    cl_uint numDevices = 0;

    auto deviceSourceParam = this->pickParam(CL_D3D10_DEVICE_KHR, CL_D3D11_DEVICE_KHR);
    auto deviceSetParam = this->pickParam(CL_PREFERRED_DEVICES_FOR_D3D10_KHR, CL_PREFERRED_DEVICES_FOR_D3D11_KHR);

    cl_int retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, &this->dummyD3DBuffer,
                                                 deviceSetParam, 0, &device, &numDevices);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(expectedDevice, device);
    EXPECT_EQ(1u, numDevices);

    device = 0;
    numDevices = 0;
    deviceSetParam = this->pickParam(CL_ALL_DEVICES_FOR_D3D10_KHR, CL_ALL_DEVICES_FOR_D3D11_KHR);
    retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, &this->dummyD3DBuffer,
                                          deviceSetParam, 0, &device, &numDevices);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(expectedDevice, device);
    EXPECT_EQ(1u, numDevices);

    device = 0;
    numDevices = 0;
    retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, &this->dummyD3DBuffer,
                                          CL_INVALID_OPERATION, 0, &device, &numDevices);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_NE(expectedDevice, device);
    EXPECT_EQ(0u, numDevices);
}

TYPED_TEST_P(D3DTests, GivenSpecificDeviceSourceWhenGettingDeviceIDsFromD3DThenOnlySelectedDevicesAreReturned) {
    cl_device_id expectedDevice = *this->devices;
    cl_device_id device = 0;
    cl_uint numDevices = 0;

    auto deviceSourceParam = this->pickParam(CL_D3D10_DEVICE_KHR, CL_D3D11_DEVICE_KHR);
    auto deviceSetParam = this->pickParam(CL_ALL_DEVICES_FOR_D3D10_KHR, CL_ALL_DEVICES_FOR_D3D11_KHR);

    cl_int retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, &this->dummyD3DBuffer,
                                                 deviceSetParam, 0, &device, &numDevices);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(expectedDevice, device);
    EXPECT_EQ(1u, numDevices);
    EXPECT_EQ(1u, this->mockSharingFcns->getDxgiDescCalled);
    EXPECT_EQ(nullptr, this->mockSharingFcns->getDxgiDescAdapterRequested);

    device = 0;
    numDevices = 0;
    deviceSourceParam = this->pickParam(CL_D3D10_DXGI_ADAPTER_KHR, CL_D3D11_DXGI_ADAPTER_KHR);
    retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, &this->dummyD3DBuffer,
                                          deviceSetParam, 0, &device, &numDevices);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(expectedDevice, device);
    EXPECT_EQ(1u, numDevices);
    EXPECT_EQ(2u, this->mockSharingFcns->getDxgiDescCalled);
    EXPECT_NE(nullptr, this->mockSharingFcns->getDxgiDescAdapterRequested);

    device = 0;
    numDevices = 0;
    retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, CL_INVALID_OPERATION, &this->dummyD3DBuffer,
                                          deviceSetParam, 0, &device, &numDevices);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_NE(expectedDevice, device);
    EXPECT_EQ(0u, numDevices);
    EXPECT_EQ(2u, this->mockSharingFcns->getDxgiDescCalled);
}

TYPED_TEST_P(D3DTests, givenNonIntelVendorWhenGetDeviceIdIsCalledThenReturnError) {
    DXGI_ADAPTER_DESC desc = {{0}};
    desc.VendorId = INTEL_VENDOR_ID + 1u;
    this->mockSharingFcns->mockDxgiDesc = desc;

    cl_device_id device = 0;
    cl_uint numDevices = 0;

    auto deviceSourceParam = this->pickParam(CL_D3D10_DEVICE_KHR, CL_D3D11_DEVICE_KHR);
    auto deviceSetParam = this->pickParam(CL_ALL_DEVICES_FOR_D3D10_KHR, CL_ALL_DEVICES_FOR_D3D11_KHR);

    cl_int retVal = this->getDeviceIDsFromD3DApi(this->mockSharingFcns, this->pPlatform, deviceSourceParam, nullptr,
                                                 deviceSetParam, 0, &device, &numDevices);

    EXPECT_EQ(CL_DEVICE_NOT_FOUND, retVal);
    EXPECT_TRUE(0 == device);
    EXPECT_EQ(0u, numDevices);
    EXPECT_EQ(1u, this->mockSharingFcns->getDxgiDescCalled);
}

TYPED_TEST_P(D3DTests, WhenCreatingFromD3DBufferKhrApiThenValidBufferIsReturned) {
    cl_int retVal;
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getSharedNTHandle(_, _))
        .Times(0);

    auto memObj = this->createFromD3DBufferApi(this->context, CL_MEM_READ_WRITE, (D3DBufferObj *)&this->dummyD3DBuffer, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);

    auto buffer = castToObject<Buffer>(memObj);
    ASSERT_NE(nullptr, buffer);
    ASSERT_NE(nullptr, buffer->getSharingHandler().get());

    auto bufferObj = static_cast<D3DBuffer<TypeParam> *>(buffer->getSharingHandler().get());

    EXPECT_EQ((D3DResource *)&this->dummyD3DBuffer, *bufferObj->getResourceHandler());
    EXPECT_TRUE(buffer->getFlags() == CL_MEM_READ_WRITE);

    clReleaseMemObject(memObj);
}

TYPED_TEST_P(D3DTests, givenNV12FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 4, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_NV12, ImagePlane::PLANE_Y, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(2u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, givenSharedObjectFromInvalidContextWhen2dCreatedThenReturnCorrectCode) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    this->mockSharingFcns->mockTexture2dDesc.MiscFlags = D3DResourceFlags::MISC_SHARED;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));
    cl_int retCode = 0;
    mockMM.get()->verifyValue = false;
    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, reinterpret_cast<D3DTexture2d *>(&this->dummyD3DTexture), CL_MEM_READ_WRITE, 4, &retCode));
    mockMM.get()->verifyValue = true;
    EXPECT_EQ(nullptr, image.get());
    EXPECT_EQ(retCode, CL_INVALID_D3D11_RESOURCE_KHR);
}

TYPED_TEST_P(D3DTests, givenSharedObjectFromInvalidContextAndNTHandleWhen2dCreatedThenReturnCorrectCode) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    this->mockSharingFcns->mockTexture2dDesc.MiscFlags = D3DResourceFlags::MISC_SHARED_NTHANDLE;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));
    cl_int retCode = 0;
    mockMM.get()->verifyValue = false;
    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, reinterpret_cast<D3DTexture2d *>(&this->dummyD3DTexture), CL_MEM_READ_WRITE, 4, &retCode));
    mockMM.get()->verifyValue = true;
    EXPECT_EQ(nullptr, image.get());
    EXPECT_EQ(retCode, CL_INVALID_D3D11_RESOURCE_KHR);
}

TYPED_TEST_P(D3DTests, givenSharedObjectAndAlocationFailedWhen2dCreatedThenReturnCorrectCode) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    this->mockSharingFcns->mockTexture2dDesc.MiscFlags = D3DResourceFlags::MISC_SHARED;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));
    cl_int retCode = 0;
    mockMM.get()->failAlloc = true;
    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, reinterpret_cast<D3DTexture2d *>(&this->dummyD3DTexture), CL_MEM_READ_WRITE, 4, &retCode));
    mockMM.get()->failAlloc = false;
    EXPECT_EQ(nullptr, image.get());
    EXPECT_EQ(retCode, CL_OUT_OF_HOST_MEMORY);
}

TYPED_TEST_P(D3DTests, givenSharedObjectAndNTHandleAndAllocationFailedWhen2dCreatedThenReturnCorrectCode) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    this->mockSharingFcns->mockTexture2dDesc.MiscFlags = D3DResourceFlags::MISC_SHARED_NTHANDLE;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));
    cl_int retCode = 0;
    mockMM.get()->failAlloc = true;
    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, reinterpret_cast<D3DTexture2d *>(&this->dummyD3DTexture), CL_MEM_READ_WRITE, 4, &retCode));
    mockMM.get()->failAlloc = false;
    EXPECT_EQ(nullptr, image.get());
    EXPECT_EQ(retCode, CL_OUT_OF_HOST_MEMORY);
}

TYPED_TEST_P(D3DTests, givenNV12FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_NV12;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 7, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_NV12, ImagePlane::PLANE_UV, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(3u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, givenP010FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_P010;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 4, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_P010, ImagePlane::PLANE_Y, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(2u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, givenP010FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_P010;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 7, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_P010, ImagePlane::PLANE_UV, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(3u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, givenP016FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_P016;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 4, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_P016, ImagePlane::PLANE_Y, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(2u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, givenP016FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams) {
    this->mockSharingFcns->mockTexture2dDesc.Format = DXGI_FORMAT_P016;
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto image = std::unique_ptr<Image>(D3DTexture<TypeParam>::create2d(this->context, (D3DTexture2d *)&this->dummyD3DTexture, CL_MEM_READ_WRITE, 7, nullptr));
    ASSERT_NE(nullptr, image.get());

    auto expectedFormat = D3DTexture<TypeParam>::findYuvSurfaceFormatInfo(DXGI_FORMAT_P016, ImagePlane::PLANE_UV, CL_MEM_READ_WRITE);
    EXPECT_TRUE(memcmp(expectedFormat, &image->getSurfaceFormatInfo(), sizeof(SurfaceFormatInfo)) == 0);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(3u, mockGmmResInfo->arrayIndexPassedToGetOffset);
}

TYPED_TEST_P(D3DTests, WhenCreatingFromD3D2dTextureKhrApiThenValidImageIsReturned) {
    cl_int retVal;
    EXPECT_CALL(*this->mockSharingFcns, createQuery(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getSharedNTHandle(_, _))
        .Times(0);

    auto memObj = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, 1, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(0u, mockGmmResInfo->arrayIndexPassedToGetOffset);

    auto image = castToObject<Image>(memObj);
    ASSERT_NE(nullptr, image);
    ASSERT_NE(nullptr, image->getSharingHandler().get());

    auto textureObj = static_cast<D3DTexture<TypeParam> *>(image->getSharingHandler().get());

    EXPECT_EQ((D3DResource *)&this->dummyD3DTexture, *textureObj->getResourceHandler());
    EXPECT_TRUE(image->getFlags() == CL_MEM_READ_WRITE);
    EXPECT_TRUE(image->getImageDesc().image_type == CL_MEM_OBJECT_IMAGE2D);
    EXPECT_EQ(1u, textureObj->getSubresource());

    clReleaseMemObject(memObj);
}

TYPED_TEST_P(D3DTests, WhenCreatingFromD3D3dTextureKhrApiThenValidImageIsReturned) {
    cl_int retVal;
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getSharedNTHandle(_, _))
        .Times(0);
    EXPECT_CALL(*this->mockSharingFcns, createQuery(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getTexture3dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture3dDesc));

    auto memObj = this->createFromD3DTexture3DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture3d *)&this->dummyD3DTexture, 1, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(1u, mockGmmResInfo->getOffsetCalled);
    EXPECT_EQ(0u, mockGmmResInfo->arrayIndexPassedToGetOffset);

    auto image = castToObject<Image>(memObj);
    ASSERT_NE(nullptr, image);
    ASSERT_NE(nullptr, image->getSharingHandler().get());

    auto textureObj = static_cast<D3DTexture<TypeParam> *>(image->getSharingHandler().get());

    EXPECT_EQ((D3DResource *)&this->dummyD3DTexture, *textureObj->getResourceHandler());
    EXPECT_TRUE(image->getFlags() == CL_MEM_READ_WRITE);
    EXPECT_TRUE(image->getImageDesc().image_type == CL_MEM_OBJECT_IMAGE3D);
    EXPECT_EQ(1u, textureObj->getSubresource());

    clReleaseMemObject(memObj);
}

TYPED_TEST_P(D3DTests, givenSharedResourceFlagWhenCreateBufferThenStagingBufferEqualsPassedBuffer) {
    this->mockSharingFcns->mockBufferDesc.MiscFlags = D3DResourceFlags::MISC_SHARED;

    ::testing::InSequence is;
    EXPECT_CALL(*this->mockSharingFcns, getBufferDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockBufferDesc));
    EXPECT_CALL(*this->mockSharingFcns, createBuffer(_, _))
        .Times(0);
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle((D3DBufferObj *)&this->dummyD3DBuffer, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, addRef((D3DBufferObj *)&this->dummyD3DBuffer))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, createQuery(_))
        .Times(1)
        .WillOnce(SetArgPointee<0>((D3DQuery *)1));

    auto buffer = std::unique_ptr<Buffer>(D3DBuffer<TypeParam>::create(this->context, (D3DBufferObj *)&this->dummyD3DBuffer, CL_MEM_READ_WRITE, nullptr));
    ASSERT_NE(nullptr, buffer.get());
    auto d3dBuffer = static_cast<D3DBuffer<TypeParam> *>(buffer->getSharingHandler().get());
    ASSERT_NE(nullptr, d3dBuffer);

    EXPECT_NE(nullptr, d3dBuffer->getQuery());
    EXPECT_TRUE(d3dBuffer->isSharedResource());
    EXPECT_EQ(&this->dummyD3DBuffer, d3dBuffer->getResourceStaging());

    EXPECT_CALL(*this->mockSharingFcns, release((D3DBufferObj *)&this->dummyD3DBuffer))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, release((D3DQuery *)d3dBuffer->getQuery()))
        .Times(1);
}

TYPED_TEST_P(D3DTests, givenNonSharedResourceFlagWhenCreateBufferThenCreateNewStagingBuffer) {
    ::testing::InSequence is;
    EXPECT_CALL(*this->mockSharingFcns, createBuffer(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>((D3DBufferObj *)&this->dummyD3DBufferStaging));
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle((D3DBufferObj *)&this->dummyD3DBufferStaging, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, addRef((D3DBufferObj *)&this->dummyD3DBuffer))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, createQuery(_))
        .Times(1)
        .WillOnce(SetArgPointee<0>((D3DQuery *)1));

    auto buffer = std::unique_ptr<Buffer>(D3DBuffer<TypeParam>::create(this->context, (D3DBufferObj *)&this->dummyD3DBuffer, CL_MEM_READ_WRITE, nullptr));
    ASSERT_NE(nullptr, buffer.get());
    auto d3dBuffer = static_cast<D3DBuffer<TypeParam> *>(buffer->getSharingHandler().get());
    ASSERT_NE(nullptr, d3dBuffer);

    EXPECT_NE(nullptr, d3dBuffer->getQuery());
    EXPECT_FALSE(d3dBuffer->isSharedResource());
    EXPECT_EQ(&this->dummyD3DBufferStaging, d3dBuffer->getResourceStaging());

    EXPECT_CALL(*this->mockSharingFcns, release((D3DBufferObj *)&this->dummyD3DBufferStaging))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, release((D3DBufferObj *)&this->dummyD3DBuffer))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, release((D3DQuery *)d3dBuffer->getQuery()))
        .Times(1);
}

TYPED_TEST_P(D3DTests, givenNonSharedResourceBufferWhenAcquiredThenCopySubregion) {
    this->context->setInteropUserSyncEnabled(true);

    ::testing::InSequence is;
    EXPECT_CALL(*this->mockSharingFcns, createBuffer(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>((D3DBufferObj *)&this->dummyD3DBufferStaging));
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getDeviceContext(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, copySubresourceRegion((D3DBufferObj *)&this->dummyD3DBufferStaging, 0u, (D3DBufferObj *)&this->dummyD3DBuffer, 0u))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, flushAndWait(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, releaseDeviceContext(_))
        .Times(1);

    EXPECT_CALL(*this->mockSharingFcns, getDeviceContext(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, copySubresourceRegion((D3DBufferObj *)&this->dummyD3DBuffer, 0u, (D3DBufferObj *)&this->dummyD3DBufferStaging, 0u))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, releaseDeviceContext(_))
        .Times(1);

    auto buffer = std::unique_ptr<Buffer>(D3DBuffer<TypeParam>::create(this->context, (D3DBufferObj *)&this->dummyD3DBuffer, CL_MEM_READ_WRITE, nullptr));
    ASSERT_NE(nullptr, buffer.get());
    cl_mem bufferMem = (cl_mem)buffer.get();

    // acquireCount == 0, acquire
    EXPECT_EQ(0u, buffer->acquireCount);
    auto retVal = this->enqueueAcquireD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(1u, buffer->acquireCount);

    // acquireCount == 1, don't acquire
    retVal = this->enqueueAcquireD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(this->pickParam(CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR, CL_D3D11_RESOURCE_ALREADY_ACQUIRED_KHR), retVal);
    EXPECT_EQ(1u, buffer->acquireCount);

    retVal = this->enqueueReleaseD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    // acquireCount == 0
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(0u, buffer->acquireCount);

    retVal = this->enqueueReleaseD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(this->pickParam(CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR, CL_D3D11_RESOURCE_NOT_ACQUIRED_KHR), retVal);
    EXPECT_EQ(0u, buffer->acquireCount);
}

TYPED_TEST_P(D3DTests, givenSharedResourceBufferWhenAcquiredThenDontCopySubregion) {
    this->context->setInteropUserSyncEnabled(true);
    this->mockSharingFcns->mockBufferDesc.MiscFlags = D3DResourceFlags::MISC_SHARED;

    ::testing::InSequence is;
    EXPECT_CALL(*this->mockSharingFcns, getBufferDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockBufferDesc));
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getDeviceContext(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, copySubresourceRegion(_, _, _, _))
        .Times(0);
    EXPECT_CALL(*this->mockSharingFcns, flushAndWait(_))
        .Times(0);
    EXPECT_CALL(*this->mockSharingFcns, releaseDeviceContext(_))
        .Times(1);

    auto buffer = std::unique_ptr<Buffer>(D3DBuffer<TypeParam>::create(this->context, (D3DBufferObj *)&this->dummyD3DBuffer, CL_MEM_READ_WRITE, nullptr));
    ASSERT_NE(nullptr, buffer.get());
    cl_mem bufferMem = (cl_mem)buffer.get();

    auto retVal = this->enqueueAcquireD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);

    retVal = this->enqueueReleaseD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TYPED_TEST_P(D3DTests, givenSharedResourceBufferAndInteropUserSyncDisabledWhenAcquiredThenFlushOnAcquire) {
    this->context->setInteropUserSyncEnabled(false);
    this->mockSharingFcns->mockBufferDesc.MiscFlags = D3DResourceFlags::MISC_SHARED;

    ::testing::InSequence is;
    EXPECT_CALL(*this->mockSharingFcns, getBufferDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockBufferDesc));
    EXPECT_CALL(*this->mockSharingFcns, getSharedHandle(_, _))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, getDeviceContext(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, copySubresourceRegion(_, _, _, _))
        .Times(0);
    EXPECT_CALL(*this->mockSharingFcns, flushAndWait(_))
        .Times(1);
    EXPECT_CALL(*this->mockSharingFcns, releaseDeviceContext(_))
        .Times(1);

    auto buffer = std::unique_ptr<Buffer>(D3DBuffer<TypeParam>::create(this->context, (D3DBufferObj *)&this->dummyD3DBuffer, CL_MEM_READ_WRITE, nullptr));
    ASSERT_NE(nullptr, buffer.get());
    cl_mem bufferMem = (cl_mem)buffer.get();

    auto retVal = this->enqueueAcquireD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);

    retVal = this->enqueueReleaseD3DObjectsApi(this->mockSharingFcns, this->cmdQ, 1, &bufferMem, 0, nullptr, nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TYPED_TEST_P(D3DTests, WhenGettingPreferD3DSharedResourcesThenCorrectValueIsReturned) {
    auto ctx = std::unique_ptr<MockContext>(new MockContext());
    cl_bool retBool = 0;
    size_t size = 0;
    auto param = this->pickParam(CL_CONTEXT_D3D10_PREFER_SHARED_RESOURCES_KHR, CL_CONTEXT_D3D11_PREFER_SHARED_RESOURCES_KHR);

    ctx->preferD3dSharedResources = 1u;
    auto retVal = ctx->getInfo(param, sizeof(retBool), &retBool, &size);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(sizeof(cl_bool), size);
    EXPECT_EQ(1u, retBool);

    ctx->preferD3dSharedResources = 0u;
    retVal = ctx->getInfo(param, sizeof(retBool), &retBool, &size);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(sizeof(cl_bool), size);
    EXPECT_EQ(0u, retBool);
}

TYPED_TEST_P(D3DTests, WhenGettingD3DResourceInfoFromMemObjThenCorrectInfoIsReturned) {
    auto memObj = this->createFromD3DBufferApi(this->context, CL_MEM_READ_WRITE, (D3DBufferObj *)&this->dummyD3DBuffer, nullptr);
    ASSERT_NE(nullptr, memObj);
    auto param = this->pickParam(CL_MEM_D3D10_RESOURCE_KHR, CL_MEM_D3D11_RESOURCE_KHR);

    void *retBuffer = nullptr;
    size_t retSize = 0;
    clGetMemObjectInfo(memObj, param, sizeof(D3DBufferObj), &retBuffer, &retSize);
    EXPECT_EQ(sizeof(D3DBufferObj), retSize);
    EXPECT_EQ(&this->dummyD3DBuffer, retBuffer);

    clReleaseMemObject(memObj);
}

TYPED_TEST_P(D3DTests, WhenGettingD3DSubresourceInfoFromMemObjThenCorrectInfoIsReturned) {
    cl_int retVal;
    cl_uint subresource = 1u;
    auto param = this->pickParam(CL_IMAGE_D3D10_SUBRESOURCE_KHR, CL_IMAGE_D3D11_SUBRESOURCE_KHR);

    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto memObj = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);

    cl_uint retSubresource = 0;
    size_t retSize = 0;
    clGetImageInfo(memObj, param, sizeof(cl_uint), &retSubresource, &retSize);
    EXPECT_EQ(sizeof(cl_uint), retSize);
    EXPECT_EQ(subresource, retSubresource);

    clReleaseMemObject(memObj);
}

TYPED_TEST_P(D3DTests, givenTheSameD3DBufferWhenNextCreateIsCalledThenFail) {
    cl_int retVal;

    EXPECT_EQ(0u, this->mockSharingFcns->getTrackedResourcesVector()->size());
    auto memObj = this->createFromD3DBufferApi(this->context, CL_MEM_READ_WRITE, (D3DBufferObj *)&this->dummyD3DBuffer, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, this->mockSharingFcns->getTrackedResourcesVector()->size());
    EXPECT_EQ(0u, this->mockSharingFcns->getTrackedResourcesVector()->at(0).second);
    auto memObj2 = this->createFromD3DBufferApi(this->context, CL_MEM_READ_WRITE, (D3DBufferObj *)&this->dummyD3DBuffer, &retVal);
    EXPECT_EQ(nullptr, memObj2);
    EXPECT_EQ(this->pickParam(CL_INVALID_D3D10_RESOURCE_KHR, CL_INVALID_D3D11_RESOURCE_KHR), retVal);
    EXPECT_EQ(1u, this->mockSharingFcns->getTrackedResourcesVector()->size());

    clReleaseMemObject(memObj);
    EXPECT_EQ(0u, this->mockSharingFcns->getTrackedResourcesVector()->size());
}

TYPED_TEST_P(D3DTests, givenD3DTextureWithTheSameSubresourceWhenNextCreateIsCalledThenFail) {
    cl_int retVal;
    cl_uint subresource = 1;

    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    EXPECT_EQ(0u, this->mockSharingFcns->getTrackedResourcesVector()->size());
    auto memObj = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, this->mockSharingFcns->getTrackedResourcesVector()->size());
    auto memObj2 = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    EXPECT_EQ(nullptr, memObj2);
    EXPECT_EQ(this->pickParam(CL_INVALID_D3D10_RESOURCE_KHR, CL_INVALID_D3D11_RESOURCE_KHR), retVal);
    EXPECT_EQ(1u, this->mockSharingFcns->getTrackedResourcesVector()->size());

    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    subresource++;
    this->setupMockGmm(); // setup new mock for new resource
    auto memObj3 = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    ASSERT_NE(nullptr, memObj);
    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(2u, this->mockSharingFcns->getTrackedResourcesVector()->size());

    clReleaseMemObject(memObj);
    EXPECT_EQ(1u, this->mockSharingFcns->getTrackedResourcesVector()->size());
    clReleaseMemObject(memObj3);
    EXPECT_EQ(0u, this->mockSharingFcns->getTrackedResourcesVector()->size());
}

TYPED_TEST_P(D3DTests, givenInvalidSubresourceWhenCreateTexture2dIsCalledThenFail) {
    cl_int retVal;
    this->mockSharingFcns->mockTexture2dDesc.ArraySize = 4;
    this->mockSharingFcns->mockTexture2dDesc.MipLevels = 4;
    cl_uint subresource = 16;

    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    auto memObj = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    EXPECT_EQ(nullptr, memObj);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);

    EXPECT_CALL(*this->mockSharingFcns, getTexture2dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture2dDesc));

    subresource = 20;
    memObj = this->createFromD3DTexture2DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture2d *)&this->dummyD3DTexture, subresource, &retVal);
    EXPECT_EQ(nullptr, memObj);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);
}

TYPED_TEST_P(D3DTests, givenInvalidSubresourceWhenCreateTexture3dIsCalledThenFail) {
    cl_int retVal;
    this->mockSharingFcns->mockTexture3dDesc.MipLevels = 4;
    cl_uint subresource = 16;

    EXPECT_CALL(*this->mockSharingFcns, getTexture3dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture3dDesc));

    auto memObj = this->createFromD3DTexture3DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture3d *)&this->dummyD3DTexture, subresource, &retVal);
    EXPECT_EQ(nullptr, memObj);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);

    EXPECT_CALL(*this->mockSharingFcns, getTexture3dDesc(_, _))
        .Times(1)
        .WillOnce(SetArgPointee<0>(this->mockSharingFcns->mockTexture3dDesc));

    subresource = 20;
    memObj = this->createFromD3DTexture3DApi(this->context, CL_MEM_READ_WRITE, (D3DTexture3d *)&this->dummyD3DTexture, subresource, &retVal);
    EXPECT_EQ(nullptr, memObj);
    EXPECT_EQ(CL_INVALID_VALUE, retVal);
}

TYPED_TEST_P(D3DTests, givenPackedFormatWhenLookingForSurfaceFormatWithPackedNotSupportedThenReturnNull) {
    EXPECT_GT(SurfaceFormats::packed().size(), 0u);
    for (auto &format : SurfaceFormats::packed()) {
        auto surfaceFormat = D3DSharing<TypeParam>::findSurfaceFormatInfo(format.surfaceFormat.GMMSurfaceFormat, CL_MEM_READ_ONLY, false /* supportsOcl20Features */, false /* packedSupported */);
        ASSERT_EQ(nullptr, surfaceFormat);
    }
}

TYPED_TEST_P(D3DTests, givenPackedFormatWhenLookingForSurfaceFormatWithPackedSupportedThenReturnValidFormat) {
    EXPECT_GT(SurfaceFormats::packed().size(), 0u);
    uint32_t counter = 0;
    for (auto &format : SurfaceFormats::packed()) {
        auto surfaceFormat = D3DSharing<TypeParam>::findSurfaceFormatInfo(format.surfaceFormat.GMMSurfaceFormat, CL_MEM_READ_ONLY, false /* supportsOcl20Features */, true /* packedSupported */);
        ASSERT_NE(nullptr, surfaceFormat);
        counter++;
        EXPECT_EQ(&format, surfaceFormat);
    }
    EXPECT_NE(counter, 0U);
}

TYPED_TEST_P(D3DTests, givenReadonlyFormatWhenLookingForSurfaceFormatThenReturnValidFormat) {
    EXPECT_GT(SurfaceFormats::readOnly12().size(), 0u);
    for (auto &format : SurfaceFormats::readOnly12()) {
        // only RGBA, BGRA, RG, R allowed for D3D
        if (format.OCLImageFormat.image_channel_order == CL_RGBA ||
            format.OCLImageFormat.image_channel_order == CL_BGRA ||
            format.OCLImageFormat.image_channel_order == CL_RG ||
            format.OCLImageFormat.image_channel_order == CL_R) {
            auto surfaceFormat = D3DSharing<TypeParam>::findSurfaceFormatInfo(format.surfaceFormat.GMMSurfaceFormat, CL_MEM_READ_ONLY, false /* supportsOcl20Features */, true);
            ASSERT_NE(nullptr, surfaceFormat);
            EXPECT_EQ(&format, surfaceFormat);
        }
    }
}

TYPED_TEST_P(D3DTests, givenWriteOnlyFormatWhenLookingForSurfaceFormatThenReturnValidFormat) {
    EXPECT_GT(SurfaceFormats::writeOnly().size(), 0u);
    for (auto &format : SurfaceFormats::writeOnly()) {
        // only RGBA, BGRA, RG, R allowed for D3D
        if (format.OCLImageFormat.image_channel_order == CL_RGBA ||
            format.OCLImageFormat.image_channel_order == CL_BGRA ||
            format.OCLImageFormat.image_channel_order == CL_RG ||
            format.OCLImageFormat.image_channel_order == CL_R) {
            auto surfaceFormat = D3DSharing<TypeParam>::findSurfaceFormatInfo(format.surfaceFormat.GMMSurfaceFormat, CL_MEM_WRITE_ONLY, context->getDevice(0)->getHardwareInfo().capabilityTable.supportsOcl21Features, true);
            ASSERT_NE(nullptr, surfaceFormat);
            EXPECT_EQ(&format, surfaceFormat);
        }
    }
}

TYPED_TEST_P(D3DTests, givenReadWriteFormatWhenLookingForSurfaceFormatThenReturnValidFormat) {
    EXPECT_GT(SurfaceFormats::readWrite().size(), 0u);
    for (auto &format : SurfaceFormats::readWrite()) {
        // only RGBA, BGRA, RG, R allowed for D3D
        if (format.OCLImageFormat.image_channel_order == CL_RGBA ||
            format.OCLImageFormat.image_channel_order == CL_BGRA ||
            format.OCLImageFormat.image_channel_order == CL_RG ||
            format.OCLImageFormat.image_channel_order == CL_R) {
            auto surfaceFormat = D3DSharing<TypeParam>::findSurfaceFormatInfo(format.surfaceFormat.GMMSurfaceFormat, CL_MEM_READ_WRITE, context->getDevice(0)->getHardwareInfo().capabilityTable.supportsOcl21Features, true);
            ASSERT_NE(nullptr, surfaceFormat);
            EXPECT_EQ(&format, surfaceFormat);
        }
    }
}

REGISTER_TYPED_TEST_CASE_P(D3DTests,
                           GivenSpecificDeviceSetWhenGettingDeviceIDsFromD3DThenOnlySelectedDevicesAreReturned,
                           GivenSpecificDeviceSourceWhenGettingDeviceIDsFromD3DThenOnlySelectedDevicesAreReturned,
                           givenNonIntelVendorWhenGetDeviceIdIsCalledThenReturnError,
                           WhenCreatingFromD3DBufferKhrApiThenValidBufferIsReturned,
                           WhenCreatingFromD3D2dTextureKhrApiThenValidImageIsReturned,
                           WhenCreatingFromD3D3dTextureKhrApiThenValidImageIsReturned,
                           givenSharedResourceFlagWhenCreateBufferThenStagingBufferEqualsPassedBuffer,
                           givenNonSharedResourceFlagWhenCreateBufferThenCreateNewStagingBuffer,
                           givenNonSharedResourceBufferWhenAcquiredThenCopySubregion,
                           givenSharedResourceBufferWhenAcquiredThenDontCopySubregion,
                           givenSharedResourceBufferAndInteropUserSyncDisabledWhenAcquiredThenFlushOnAcquire,
                           WhenGettingPreferD3DSharedResourcesThenCorrectValueIsReturned,
                           WhenGettingD3DResourceInfoFromMemObjThenCorrectInfoIsReturned,
                           WhenGettingD3DSubresourceInfoFromMemObjThenCorrectInfoIsReturned,
                           givenTheSameD3DBufferWhenNextCreateIsCalledThenFail,
                           givenD3DTextureWithTheSameSubresourceWhenNextCreateIsCalledThenFail,
                           givenInvalidSubresourceWhenCreateTexture2dIsCalledThenFail,
                           givenInvalidSubresourceWhenCreateTexture3dIsCalledThenFail,
                           givenReadonlyFormatWhenLookingForSurfaceFormatThenReturnValidFormat,
                           givenWriteOnlyFormatWhenLookingForSurfaceFormatThenReturnValidFormat,
                           givenReadWriteFormatWhenLookingForSurfaceFormatThenReturnValidFormat,
                           givenNV12FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams,
                           givenSharedObjectFromInvalidContextWhen2dCreatedThenReturnCorrectCode,
                           givenSharedObjectFromInvalidContextAndNTHandleWhen2dCreatedThenReturnCorrectCode,
                           givenSharedObjectAndAlocationFailedWhen2dCreatedThenReturnCorrectCode,
                           givenSharedObjectAndNTHandleAndAllocationFailedWhen2dCreatedThenReturnCorrectCode,
                           givenP010FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams,
                           givenP016FormatAndEvenPlaneWhen2dCreatedThenSetPlaneParams,
                           givenNV12FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams,
                           givenP010FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams,
                           givenP016FormatAndOddPlaneWhen2dCreatedThenSetPlaneParams,
                           givenPackedFormatWhenLookingForSurfaceFormatWithPackedNotSupportedThenReturnNull,
                           givenPackedFormatWhenLookingForSurfaceFormatWithPackedSupportedThenReturnValidFormat);

INSTANTIATE_TYPED_TEST_CASE_P(D3DSharingTests, D3DTests, D3DTypes);

TEST(D3DSurfaceTest, givenD3DSurfaceWhenInvalidMemObjectIsPassedToValidateUpdateDataThenInvalidMemObjectErrorIsReturned) {
    class MockD3DSurface : public D3DSurface {
      public:
        MockD3DSurface(Context *context, cl_dx9_surface_info_khr *surfaceInfo, D3DTypesHelper::D3D9::D3DTexture2d *surfaceStaging, cl_uint plane,
                       ImagePlane imagePlane, cl_dx9_media_adapter_type_khr adapterType, bool sharedResource, bool lockable) : D3DSurface(context, surfaceInfo, surfaceStaging, plane,
                                                                                                                                          imagePlane, adapterType, sharedResource, lockable) {}
    };
    MockContext context;
    cl_dx9_surface_info_khr surfaceInfo = {};
    ImagePlane imagePlane = ImagePlane::NO_PLANE;
    std::unique_ptr<D3DSurface> surface(new MockD3DSurface(&context, &surfaceInfo, nullptr, 0, imagePlane, 0, false, false));

    MockBuffer buffer;
    UpdateData updateData{context.getDevice(0)->getRootDeviceIndex()};
    updateData.memObject = &buffer;
    auto result = surface->validateUpdateData(updateData);
    EXPECT_EQ(CL_INVALID_MEM_OBJECT, result);
}

TEST(D3D9, givenD3D9BuilderAndExtensionEnableTrueWhenGettingExtensionsThenCorrectExtensionsListIsReturned) {
    auto builderFactory = std::make_unique<D3DSharingBuilderFactory<D3DTypesHelper::D3D9>>();
    builderFactory.get()->extensionEnabled = true;
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::HasSubstr(std::string("cl_intel_dx9_media_sharing")));
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::HasSubstr(std::string("cl_khr_dx9_media_sharing")));
}

TEST(D3D9, givenD3D9BuilderAndExtensionEnableFalseWhenGettingExtensionsThenDx9MediaSheringExtensionsAreNotReturned) {
    auto builderFactory = std::make_unique<D3DSharingBuilderFactory<D3DTypesHelper::D3D9>>();
    builderFactory.get()->extensionEnabled = false;
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::Not(testing::HasSubstr(std::string("cl_intel_dx9_media_sharing"))));
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::Not(testing::HasSubstr(std::string("cl_khr_dx9_media_sharing"))));
}

TEST(D3D10, givenD3D10BuilderWhenGettingExtensionsThenCorrectExtensionsListIsReturned) {
    auto builderFactory = std::make_unique<D3DSharingBuilderFactory<D3DTypesHelper::D3D10>>();
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::HasSubstr(std::string("cl_khr_d3d10_sharing")));
}

TEST(D3D11, givenD3D11BuilderWhenGettingExtensionsThenCorrectExtensionsListIsReturned) {
    auto builderFactory = std::make_unique<D3DSharingBuilderFactory<D3DTypesHelper::D3D11>>();
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::HasSubstr(std::string("cl_khr_d3d11_sharing")));
    EXPECT_THAT(builderFactory->getExtensions(nullptr), testing::HasSubstr(std::string("cl_intel_d3d11_nv12_media_sharing")));
}

TEST(D3DSharingFactory, givenEnabledFormatQueryAndFactoryWithD3DSharingsWhenGettingExtensionFunctionAddressThenFormatQueryFunctionsAreReturned) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.EnableFormatQuery.set(true);
    SharingFactoryMock sharingFactory;

    auto function = sharingFactory.getExtensionFunctionAddress("clGetSupportedDX9MediaSurfaceFormatsINTEL");
    EXPECT_EQ(reinterpret_cast<void *>(clGetSupportedDX9MediaSurfaceFormatsINTEL), function);

    function = sharingFactory.getExtensionFunctionAddress("clGetSupportedD3D10TextureFormatsINTEL");
    EXPECT_EQ(reinterpret_cast<void *>(clGetSupportedD3D10TextureFormatsINTEL), function);

    function = sharingFactory.getExtensionFunctionAddress("clGetSupportedD3D11TextureFormatsINTEL");
    EXPECT_EQ(reinterpret_cast<void *>(clGetSupportedD3D11TextureFormatsINTEL), function);
}

TEST(D3D9SharingFactory, givenDriverInfoWhenVerifyExtensionSupportThenExtensionEnableIsSetCorrect) {
    class MockDriverInfo : public DriverInfo {
      public:
        bool getMediaSharingSupport() override { return support; };
        bool support = true;
    };
    class MockSharingFactory : public SharingFactory {
      public:
        MockSharingFactory() {
            memcpy_s(savedState, sizeof(savedState), sharingContextBuilder, sizeof(sharingContextBuilder));
        }
        ~MockSharingFactory() {
            memcpy_s(sharingContextBuilder, sizeof(sharingContextBuilder), savedState, sizeof(savedState));
        }

        void prepare() {
            for (auto &builder : sharingContextBuilder) {
                builder = nullptr;
            }
            d3d9SharingBuilderFactory = std::make_unique<D3DSharingBuilderFactory<D3DTypesHelper::D3D9>>();
            sharingContextBuilder[SharingType::D3D9_SHARING] = d3d9SharingBuilderFactory.get();
        }

        using SharingFactory::sharingContextBuilder;
        std::unique_ptr<D3DSharingBuilderFactory<D3DTypesHelper::D3D9>> d3d9SharingBuilderFactory;
        decltype(SharingFactory::sharingContextBuilder) savedState;
    };

    auto driverInfo = std::make_unique<MockDriverInfo>();
    auto mockSharingFactory = std::make_unique<MockSharingFactory>();
    mockSharingFactory->prepare();

    driverInfo->support = true;
    mockSharingFactory->verifyExtensionSupport(driverInfo.get());
    EXPECT_TRUE(mockSharingFactory->d3d9SharingBuilderFactory->extensionEnabled);

    driverInfo->support = false;
    mockSharingFactory->verifyExtensionSupport(driverInfo.get());
    EXPECT_FALSE(mockSharingFactory->d3d9SharingBuilderFactory->extensionEnabled);
}
} // namespace NEO
