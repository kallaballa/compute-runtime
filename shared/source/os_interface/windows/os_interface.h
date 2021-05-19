/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/os_interface/os_interface.h"
#include "shared/source/os_interface/windows/wddm/wddm.h"
#include "shared/source/os_interface/windows/windows_wrapper.h"

#include "profileapi.h"
#include "umKmInc/sharedata.h"
#include <d3dkmthk.h>

#include <memory>

namespace NEO {
class Wddm;
class WddmMemoryOperationsHandler;

class OSInterface::OSInterfaceImpl {
  public:
    OSInterfaceImpl();
    virtual ~OSInterfaceImpl() = default;
    Wddm *getWddm() const;
    void setWddm(Wddm *wddm);
    D3DKMT_HANDLE getAdapterHandle() const;
    D3DKMT_HANDLE getDeviceHandle() const;
    PFND3DKMT_ESCAPE getEscapeHandle() const;
    uint32_t getHwContextId() const;

    MOCKABLE_VIRTUAL HANDLE createEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                                        LPCSTR lpName);
    MOCKABLE_VIRTUAL BOOL closeHandle(HANDLE hObject);

  protected:
    std::unique_ptr<Wddm> wddm;
};
} // namespace NEO
