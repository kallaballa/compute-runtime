/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_buffer_object.h"

#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/os_interface/linux/drm_memory_manager.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_time_linux.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/source/utilities/stackvec.h"

#include "drm/i915_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <map>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace NEO {

BufferObject::BufferObject(Drm *drm, int handle, size_t size, size_t maxOsContextCount) : drm(drm), refCount(1), handle(handle), size(size), isReused(false) {
    this->tiling_mode = I915_TILING_NONE;
    this->lockedAddress = nullptr;

    perContextVmsUsed = drm->isPerContextVMRequired();

    if (perContextVmsUsed) {
        bindInfo.resize(maxOsContextCount);
        for (auto &iter : bindInfo) {
            iter.fill(false);
        }
    } else {
        bindInfo.resize(1);
        bindInfo[0].fill(false);
    }
}

uint32_t BufferObject::getRefCount() const {
    return this->refCount.load();
}

bool BufferObject::close() {
    drm_gem_close close = {};
    close.handle = this->handle;

    PRINT_DEBUG_STRING(DebugManager.flags.PrintBOCreateDestroyResult.get(), stdout, "Calling gem close on handle: BO-%d\n", this->handle);

    int ret = this->drm->ioctl(DRM_IOCTL_GEM_CLOSE, &close);
    if (ret != 0) {
        int err = errno;
        PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "ioctl(GEM_CLOSE) failed with %d. errno=%d(%s)\n", ret, err, strerror(err));
        DEBUG_BREAK_IF(true);
        return false;
    }

    this->handle = -1;

    return true;
}

int BufferObject::wait(int64_t timeoutNs) {
    drm_i915_gem_wait wait = {};
    wait.bo_handle = this->handle;
    wait.timeout_ns = -1;

    int ret = this->drm->ioctl(DRM_IOCTL_I915_GEM_WAIT, &wait);
    if (ret != 0) {
        int err = errno;
        PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "ioctl(I915_GEM_WAIT) failed with %d. errno=%d(%s)\n", ret, err, strerror(err));
    }
    UNRECOVERABLE_IF(ret != 0);

    return ret;
}

bool BufferObject::setTiling(uint32_t mode, uint32_t stride) {
    if (this->tiling_mode == mode) {
        return true;
    }

    drm_i915_gem_set_tiling set_tiling = {};
    set_tiling.handle = this->handle;
    set_tiling.tiling_mode = mode;
    set_tiling.stride = stride;

    if (this->drm->ioctl(DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling) != 0) {
        return false;
    }

    this->tiling_mode = set_tiling.tiling_mode;

    return set_tiling.tiling_mode == mode;
}

void BufferObject::fillExecObject(drm_i915_gem_exec_object2 &execObject, OsContext *osContext, uint32_t vmHandleId, uint32_t drmContextId) {
    execObject.handle = this->handle;
    execObject.relocation_count = 0; //No relocations, we are SoftPinning
    execObject.relocs_ptr = 0ul;
    execObject.alignment = 0;
    execObject.offset = this->gpuAddress;
    execObject.flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
    execObject.rsvd1 = drmContextId;
    execObject.rsvd2 = 0;

    this->fillExecObjectImpl(execObject, osContext, vmHandleId);
}

int BufferObject::exec(uint32_t used, size_t startOffset, unsigned int flags, bool requiresCoherency, OsContext *osContext, uint32_t vmHandleId, uint32_t drmContextId, BufferObject *const residency[], size_t residencyCount, drm_i915_gem_exec_object2 *execObjectsStorage) {
    for (size_t i = 0; i < residencyCount; i++) {
        residency[i]->fillExecObject(execObjectsStorage[i], osContext, vmHandleId, drmContextId);
    }
    this->fillExecObject(execObjectsStorage[residencyCount], osContext, vmHandleId, drmContextId);

    drm_i915_gem_execbuffer2 execbuf{};
    execbuf.buffers_ptr = reinterpret_cast<uintptr_t>(execObjectsStorage);
    execbuf.buffer_count = static_cast<uint32_t>(residencyCount + 1u);
    execbuf.batch_start_offset = static_cast<uint32_t>(startOffset);
    execbuf.batch_len = alignUp(used, 8);
    execbuf.flags = flags;
    execbuf.rsvd1 = drmContextId;

    if (DebugManager.flags.PrintExecutionBuffer.get()) {
        printExecutionBuffer(execbuf, residencyCount, execObjectsStorage, residency);
    }

    int ret = this->drm->ioctl(DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
    if (ret == 0) {
        return 0;
    }

    int err = this->drm->getErrno();
    PRINT_DEBUG_STRING(DebugManager.flags.PrintDebugMessages.get(), stderr, "ioctl(I915_GEM_EXECBUFFER2) failed with %d. errno=%d(%s)\n", ret, err, strerror(err));
    return err;
}

void BufferObject::bind(OsContext *osContext, uint32_t vmHandleId) {
    auto contextId = perContextVmsUsed ? osContext->getContextId() : 0;
    if (!this->bindInfo[contextId][vmHandleId]) {
        auto ret = this->drm->bindBufferObject(osContext, vmHandleId, this);
        auto err = this->drm->getErrno();
        PRINT_DEBUG_STRING(DebugManager.flags.PrintBOBindingResult.get(), stderr, "bind BO-%d to VM %u, range: %llx - %llx, size: %lld, result: %d, errno: %d(%s)\n", this->handle, vmHandleId, this->gpuAddress, ptrOffset(this->gpuAddress, this->size), this->size, ret, err, strerror(err));
        UNRECOVERABLE_IF(ret != 0);
        this->bindInfo[contextId][vmHandleId] = true;
    }
}

void BufferObject::unbind(OsContext *osContext, uint32_t vmHandleId) {
    auto contextId = perContextVmsUsed ? osContext->getContextId() : 0;
    if (this->bindInfo[contextId][vmHandleId]) {
        auto ret = this->drm->unbindBufferObject(osContext, vmHandleId, this);
        auto err = this->drm->getErrno();
        PRINT_DEBUG_STRING(DebugManager.flags.PrintBOBindingResult.get(), stderr, "unbind BO-%d from VM %u, range: %llx - %llx, size: %lld, result: %d, errno: %d(%s)\n", this->handle, vmHandleId, this->gpuAddress, ptrOffset(this->gpuAddress, this->size), this->size, ret, err, strerror(err));
        UNRECOVERABLE_IF(ret != 0);
        this->bindInfo[contextId][vmHandleId] = false;
    }
}

void BufferObject::printExecutionBuffer(drm_i915_gem_execbuffer2 &execbuf, const size_t &residencyCount, drm_i915_gem_exec_object2 *execObjectsStorage, BufferObject *const residency[]) {
    std::stringstream logger;
    logger << "drm_i915_gem_execbuffer2 { "
           << "buffer_ptr: " + std::to_string(execbuf.buffers_ptr)
           << ", buffer_count: " + std::to_string(execbuf.buffer_count)
           << ", batch_start_offset: " + std::to_string(execbuf.batch_start_offset)
           << ", batch_len: " + std::to_string(execbuf.batch_len)
           << ", flags: " + std::to_string(execbuf.flags)
           << ", rsvd1: " + std::to_string(execbuf.rsvd1)
           << " }\n";

    size_t i;
    for (i = 0; i < residencyCount; i++) {
        logger << "Buffer Object = { handle: BO-" << execObjectsStorage[i].handle
               << ", address range: 0x" << (void *)execObjectsStorage[i].offset
               << " - 0x" << (void *)ptrOffset(execObjectsStorage[i].offset, residency[i]->peekSize())
               << ", flags: " << execObjectsStorage[i].flags
               << ", size: " << residency[i]->peekSize() << " }\n";
    }
    logger << "Command Buffer Object = { handle: BO-" << execObjectsStorage[i].handle
           << ", address range: 0x" << (void *)execObjectsStorage[i].offset
           << " - 0x" << (void *)ptrOffset(execObjectsStorage[i].offset, this->peekSize())
           << ", flags: " << execObjectsStorage[i].flags
           << ", size: " << this->peekSize() << " }\n";

    std::cout << logger.str() << std::endl;
}

int BufferObject::pin(BufferObject *const boToPin[], size_t numberOfBos, OsContext *osContext, uint32_t vmHandleId, uint32_t drmContextId) {
    auto retVal = 0;
    if (this->drm->isBindAvailable()) {
        for (auto drmIterator = 0u; drmIterator < osContext->getDeviceBitfield().size(); drmIterator++) {
            if (osContext->getDeviceBitfield().test(drmIterator)) {
                for (size_t i = 0; i < numberOfBos; i++) {
                    boToPin[i]->bind(osContext, drmIterator);
                }
            }
        }
    } else {
        StackVec<drm_i915_gem_exec_object2, maxFragmentsCount + 1> execObject(numberOfBos + 1);
        retVal = this->exec(4u, 0u, 0u, false, osContext, vmHandleId, drmContextId, boToPin, numberOfBos, &execObject[0]);
    }
    return retVal;
}

void BufferObject::addBindExtHandle(uint32_t handle) {
    bindExtHandles.push_back(handle);
}

} // namespace NEO
