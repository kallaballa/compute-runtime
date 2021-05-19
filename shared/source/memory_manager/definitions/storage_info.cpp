/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {
StorageInfo MemoryManager::createStorageInfoFromProperties(const AllocationProperties &properties) {
    return {};
}
uint32_t StorageInfo::getNumBanks() const { return 1u; }
} // namespace NEO
