/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/os_interface/os_interface.h"

#include <memory>

namespace NEO {
class Drm;

class OSInterface::OSInterfaceImpl {
  public:
    OSInterfaceImpl();
    ~OSInterfaceImpl();
    Drm *getDrm() const {
        return drm.get();
    }
    void setDrm(Drm *drm);

    bool isDebugAttachAvailable() const;

  protected:
    std::unique_ptr<Drm> drm;
};
} // namespace NEO
