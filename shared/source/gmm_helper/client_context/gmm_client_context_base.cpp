/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gmm_helper/client_context/gmm_client_context_base.h"

#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/gmm_helper/gmm_interface.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/sku_info/operations/sku_info_transfer.h"

namespace NEO {
GmmClientContextBase::GmmClientContextBase(OSInterface *osInterface, HardwareInfo *hwInfo) : hardwareInfo(hwInfo) {
    _SKU_FEATURE_TABLE gmmFtrTable = {};
    _WA_TABLE gmmWaTable = {};
    SkuInfoTransfer::transferFtrTableForGmm(&gmmFtrTable, &hwInfo->featureTable);
    SkuInfoTransfer::transferWaTableForGmm(&gmmWaTable, &hwInfo->workaroundTable);

    GMM_INIT_IN_ARGS inArgs{};
    GMM_INIT_OUT_ARGS outArgs{};

    inArgs.ClientType = GMM_CLIENT::GMM_OCL_VISTA;
    inArgs.pGtSysInfo = &hwInfo->gtSystemInfo;
    inArgs.pSkuTable = &gmmFtrTable;
    inArgs.pWaTable = &gmmWaTable;
    inArgs.Platform = hwInfo->platform;

    if (osInterface && osInterface->getDriverModel()) {
        osInterface->getDriverModel()->setGmmInputArgs(&inArgs);
    }

    auto ret = GmmInterface::initialize(&inArgs, &outArgs);

    UNRECOVERABLE_IF(ret != GMM_SUCCESS);

    clientContext = outArgs.pGmmClientContext;
}
GmmClientContextBase::~GmmClientContextBase() {
    GMM_INIT_OUT_ARGS outArgs;
    outArgs.pGmmClientContext = clientContext;

    GmmInterface::destroy(&outArgs);
};

MEMORY_OBJECT_CONTROL_STATE GmmClientContextBase::cachePolicyGetMemoryObject(GMM_RESOURCE_INFO *pResInfo, GMM_RESOURCE_USAGE_TYPE usage) {
    return clientContext->CachePolicyGetMemoryObject(pResInfo, usage);
}

GMM_RESOURCE_INFO *GmmClientContextBase::createResInfoObject(GMM_RESCREATE_PARAMS *pCreateParams) {
    return clientContext->CreateResInfoObject(pCreateParams);
}

GMM_RESOURCE_INFO *GmmClientContextBase::copyResInfoObject(GMM_RESOURCE_INFO *pSrcRes) {
    return clientContext->CopyResInfoObject(pSrcRes);
}

void GmmClientContextBase::destroyResInfoObject(GMM_RESOURCE_INFO *pResInfo) {
    clientContext->DestroyResInfoObject(pResInfo);
}

GMM_CLIENT_CONTEXT *GmmClientContextBase::getHandle() const {
    return clientContext;
}

uint8_t GmmClientContextBase::getSurfaceStateCompressionFormat(GMM_RESOURCE_FORMAT format) {
    return clientContext->GetSurfaceStateCompressionFormat(format);
}

uint8_t GmmClientContextBase::getMediaSurfaceStateCompressionFormat(GMM_RESOURCE_FORMAT format) {
    return clientContext->GetMediaSurfaceStateCompressionFormat(format);
}

} // namespace NEO
