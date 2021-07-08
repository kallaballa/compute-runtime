/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/device.h"
#include "shared/source/direct_submission/windows/wddm_direct_submission.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/os_interface/windows/os_context_win.h"
#include "shared/source/os_interface/windows/wddm/wddm.h"
#include "shared/source/os_interface/windows/wddm/wddm_interface.h"
#include "shared/source/os_interface/windows/wddm/wddm_residency_logger.h"
#include "shared/source/os_interface/windows/wddm_allocation.h"
#include "shared/source/os_interface/windows/wddm_memory_operations_handler.h"
#include "shared/source/utilities/arrayref.h"

namespace NEO {

// Initialize COMMAND_BUFFER_HEADER         Type PatchList  Streamer Perf Tag
DECLARE_COMMAND_BUFFER(CommandBufferHeader, UMD_OCL, FALSE, FALSE, PERFTAG_OCL);

template <typename GfxFamily, typename Dispatcher>
WddmDirectSubmission<GfxFamily, Dispatcher>::WddmDirectSubmission(Device &device,
                                                                  OsContext &osContext)
    : DirectSubmissionHw<GfxFamily, Dispatcher>(device, osContext) {
    osContextWin = reinterpret_cast<OsContextWin *>(&osContext);
    wddm = osContextWin->getWddm();
    commandBufferHeader = std::make_unique<COMMAND_BUFFER_HEADER_REC>();
    *(commandBufferHeader.get()) = CommandBufferHeader;
    if (device.getPreemptionMode() != PreemptionMode::Disabled) {
        commandBufferHeader->NeedsMidBatchPreEmptionSupport = true;
    }
    perfLogResidencyVariadicLog(wddm->getResidencyLogger(), "Starting Wddm ULLS. Placement ring buffer: %d semaphore %d\n",
                                DebugManager.flags.DirectSubmissionBufferPlacement.get(),
                                DebugManager.flags.DirectSubmissionSemaphorePlacement.get());
}

template <typename GfxFamily, typename Dispatcher>
WddmDirectSubmission<GfxFamily, Dispatcher>::~WddmDirectSubmission() {
    perfLogResidencyVariadicLog(wddm->getResidencyLogger(), "Stopping Wddm ULLS\n");
    if (this->ringStart) {
        this->stopRingBuffer();
        WddmDirectSubmission<GfxFamily, Dispatcher>::handleCompletionRingBuffer(ringFence.lastSubmittedFence, ringFence);
    }
    this->deallocateResources();
    wddm->getWddmInterface()->destroyMonitorFence(ringFence);
}

template <typename GfxFamily, typename Dispatcher>
bool WddmDirectSubmission<GfxFamily, Dispatcher>::allocateOsResources() {
    //for now only WDDM2.0
    UNRECOVERABLE_IF(wddm->getWddmVersion() != WddmVersion::WDDM_2_0);

    bool ret = wddm->getWddmInterface()->createMonitoredFence(ringFence);
    ringFence.currentFenceValue = 1;
    perfLogResidencyVariadicLog(wddm->getResidencyLogger(), "ULLS resource allocation finished with: %d\n", ret);
    return ret;
}

template <typename GfxFamily, typename Dispatcher>
bool WddmDirectSubmission<GfxFamily, Dispatcher>::submit(uint64_t gpuAddress, size_t size) {
    perfLogResidencyVariadicLog(wddm->getResidencyLogger(), "ULLS Submit to GPU\n");
    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandBufferHeader.get());
    pHeader->RequiresCoherency = false;

    pHeader->UmdRequestedSliceState = 0;
    pHeader->UmdRequestedEUCount = wddm->getRequestedEUCount();

    pHeader->UmdRequestedSubsliceCount = 0;
    pHeader->NeedsMidBatchPreEmptionSupport = true;

    WddmSubmitArguments submitArgs = {};
    submitArgs.contextHandle = osContextWin->getWddmContextHandle();
    submitArgs.hwQueueHandle = osContextWin->getHwQueue().handle;
    submitArgs.monitorFence = &ringFence;

    return wddm->submit(gpuAddress, size, pHeader, submitArgs);
}

template <typename GfxFamily, typename Dispatcher>
bool WddmDirectSubmission<GfxFamily, Dispatcher>::handleResidency() {
    wddm->waitOnPagingFenceFromCpu();
    perfLogResidencyVariadicLog(wddm->getResidencyLogger(), "ULLS residency wait exit\n");
    return true;
}

template <typename GfxFamily, typename Dispatcher>
void WddmDirectSubmission<GfxFamily, Dispatcher>::handleSwitchRingBuffers() {
    if (this->ringStart) {
        if (this->completionRingBuffers[this->currentRingBuffer] != 0) {
            MonitoredFence &currentFence = osContextWin->getResidencyController().getMonitoredFence();
            handleCompletionRingBuffer(this->completionRingBuffers[this->currentRingBuffer], currentFence);
        }
    }
}

template <typename GfxFamily, typename Dispatcher>
uint64_t WddmDirectSubmission<GfxFamily, Dispatcher>::updateTagValue() {
    MonitoredFence &currentFence = osContextWin->getResidencyController().getMonitoredFence();

    currentFence.lastSubmittedFence = currentFence.currentFenceValue;
    currentFence.currentFenceValue++;
    this->completionRingBuffers[this->currentRingBuffer] = currentFence.lastSubmittedFence;

    return currentFence.lastSubmittedFence;
}

template <typename GfxFamily, typename Dispatcher>
void WddmDirectSubmission<GfxFamily, Dispatcher>::handleCompletionRingBuffer(uint64_t completionValue, MonitoredFence &fence) {
    wddm->waitFromCpu(completionValue, fence);
}

template <typename GfxFamily, typename Dispatcher>
void WddmDirectSubmission<GfxFamily, Dispatcher>::getTagAddressValue(TagData &tagData) {
    MonitoredFence &currentFence = osContextWin->getResidencyController().getMonitoredFence();

    tagData.tagAddress = GmmHelper::canonize(currentFence.gpuAddress);
    tagData.tagValue = currentFence.currentFenceValue;
}

} // namespace NEO
