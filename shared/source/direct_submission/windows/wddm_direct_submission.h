/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/direct_submission/direct_submission_hw.h"
#include "shared/source/os_interface/windows/windows_defs.h"
struct COMMAND_BUFFER_HEADER_REC;
typedef struct COMMAND_BUFFER_HEADER_REC COMMAND_BUFFER_HEADER;

namespace NEO {

class OsContextWin;
class Wddm;

template <typename GfxFamily, typename Dispatcher>
class WddmDirectSubmission : public DirectSubmissionHw<GfxFamily, Dispatcher> {
  public:
    WddmDirectSubmission(Device &device,
                         OsContext &osContext);

    ~WddmDirectSubmission();

  protected:
    bool allocateOsResources() override;
    bool submit(uint64_t gpuAddress, size_t size) override;

    bool handleResidency() override;
    void handleCompletionRingBuffer(uint64_t completionValue, MonitoredFence &fence);
    void handleSwitchRingBuffers() override;
    uint64_t updateTagValue() override;
    void getTagAddressValue(TagData &tagData) override;

    OsContextWin *osContextWin;
    Wddm *wddm;
    MonitoredFence ringFence;
    std::unique_ptr<COMMAND_BUFFER_HEADER> commandBufferHeader;
};
} // namespace NEO
