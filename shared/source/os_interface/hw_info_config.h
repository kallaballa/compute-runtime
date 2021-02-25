/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/helpers/hw_info.h"
#include "shared/source/unified_memory/usm_memory_support.h"

#include "igfxfmid.h"

namespace NEO {

struct HardwareInfo;
class OSInterface;
class HwInfoConfig;

extern HwInfoConfig *hwInfoConfigFactory[IGFX_MAX_PRODUCT];

class HwInfoConfig {
  public:
    static HwInfoConfig *get(PRODUCT_FAMILY product) {
        return hwInfoConfigFactory[product];
    }
    int configureHwInfo(const HardwareInfo *inHwInfo, HardwareInfo *outHwInfo, OSInterface *osIface);
    virtual int configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) = 0;
    virtual void adjustPlatformForProductFamily(HardwareInfo *hwInfo) = 0;
    virtual void adjustSamplerState(void *sampler) = 0;
    virtual uint64_t getHostMemCapabilities(const HardwareInfo *hwInfo) = 0;
    virtual uint64_t getDeviceMemCapabilities() = 0;
    virtual uint64_t getSingleDeviceSharedMemCapabilities() = 0;
    virtual uint64_t getCrossDeviceSharedMemCapabilities() = 0;
    virtual uint64_t getSharedSystemMemCapabilities() = 0;
    virtual bool isEvenContextCountRequired() = 0;
    virtual void convertTimestampsFromOaToCsDomain(uint64_t &timestampData) = 0;
    uint32_t threadsPerEu;
};

template <PRODUCT_FAMILY gfxProduct>
class HwInfoConfigHw : public HwInfoConfig {
  public:
    static HwInfoConfig *get() {
        static HwInfoConfigHw<gfxProduct> instance;
        return &instance;
    }
    int configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) override;
    void adjustPlatformForProductFamily(HardwareInfo *hwInfo) override;
    void adjustSamplerState(void *sampler) override;
    uint64_t getHostMemCapabilities(const HardwareInfo *hwInfo) override;
    uint64_t getDeviceMemCapabilities() override;
    uint64_t getSingleDeviceSharedMemCapabilities() override;
    uint64_t getCrossDeviceSharedMemCapabilities() override;
    uint64_t getSharedSystemMemCapabilities() override;
    bool isEvenContextCountRequired() override;
    void convertTimestampsFromOaToCsDomain(uint64_t &timestampData) override;

  protected:
    HwInfoConfigHw() = default;

    void enableRenderCompression(HardwareInfo *hwInfo);
    void enableBlitterOperationsSupport(HardwareInfo *hwInfo);
    uint64_t getHostMemCapabilitiesValue();
    bool getHostMemCapabilitiesSupported(const HardwareInfo *hwInfo);
};

template <PRODUCT_FAMILY gfxProduct>
struct EnableProductHwInfoConfig {
    typedef typename HwMapper<gfxProduct>::GfxProduct GfxProduct;

    EnableProductHwInfoConfig() {
        HwInfoConfig *pHwInfoConfig = HwInfoConfigHw<gfxProduct>::get();
        hwInfoConfigFactory[gfxProduct] = pHwInfoConfig;
        pHwInfoConfig->threadsPerEu = GfxProduct::threadsPerEu;
    }
};

} // namespace NEO
