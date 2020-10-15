/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/command_stream/csr_definitions.h"

#include "level_zero/core/source/cmdqueue/cmdqueue.h"
#include "level_zero/core/source/cmdqueue/cmdqueue_imp.h"
#include <level_zero/ze_api.h>

#include <limits>

struct _ze_fence_handle_t {};

namespace L0 {

struct Fence : _ze_fence_handle_t {
    static Fence *create(CommandQueueImp *cmdQueue, const ze_fence_desc_t *desc);
    virtual ~Fence() = default;
    virtual ze_result_t destroy() = 0;
    virtual ze_result_t hostSynchronize(uint64_t timeout) = 0;
    virtual ze_result_t queryStatus() = 0;
    virtual ze_result_t reset() = 0;

    static Fence *fromHandle(ze_fence_handle_t handle) { return static_cast<Fence *>(handle); }

    inline ze_fence_handle_t toHandle() { return this; }

    enum State : uint32_t {
        STATE_SIGNALED = 0u,
        STATE_CLEARED = std::numeric_limits<uint32_t>::max(),
        STATE_INITIAL = STATE_CLEARED
    };

    enum EnqueueState : uint32_t { ENQUEUE_NOT_READY = 0u,
                                   ENQUEUE_READY };

    NEO::GraphicsAllocation &getAllocation() const { return *allocation; }

    uint64_t getGpuAddress() {
        UNRECOVERABLE_IF(allocation == nullptr);
        return allocation->getGpuAddress();
    }

  protected:
    NEO::GraphicsAllocation *allocation = nullptr;
};

struct FenceImp : public Fence {
    FenceImp(CommandQueueImp *cmdQueueImp) : cmdQueue(cmdQueueImp) {}

    ~FenceImp() override;

    ze_result_t destroy() override {
        delete this;
        return ZE_RESULT_SUCCESS;
    }

    ze_result_t hostSynchronize(uint64_t timeout) override;

    ze_result_t queryStatus() override;

    ze_result_t reset() override;

    void initialize();

  protected:
    CommandQueueImp *cmdQueue;
};
} // namespace L0
