/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "hw_cmds.h"

namespace NEO {
class Kernel;

class GTPinHwHelper {
  public:
    static GTPinHwHelper &get(GFXCORE_FAMILY gfxCore);
    virtual uint32_t getGenVersion() = 0;
    virtual bool addSurfaceState(Kernel *pKernel, uint32_t rootDeviceIndex) = 0;
    virtual void *getSurfaceState(Kernel *pKernel, size_t bti, uint32_t rootDeviceIndex) = 0;

  protected:
    GTPinHwHelper(){};
};

template <typename GfxFamily>
class GTPinHwHelperHw : public GTPinHwHelper {
  public:
    static GTPinHwHelper &get() {
        static GTPinHwHelperHw<GfxFamily> gtpinHwHelper;
        return gtpinHwHelper;
    }
    uint32_t getGenVersion() override;
    bool addSurfaceState(Kernel *pKernel, uint32_t rootDeviceIndex) override;
    void *getSurfaceState(Kernel *pKernel, size_t bti, uint32_t rootDeviceIndex) override;

  private:
    GTPinHwHelperHw(){};
};
} // namespace NEO
