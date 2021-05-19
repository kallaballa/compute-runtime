/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <utility>
#include <vector>

namespace NEO {
class GraphicsAllocation;
using ResidencyContainer = std::vector<GraphicsAllocation *>;
using AllocationView = std::pair<uint64_t /*address*/, size_t /*size*/>;

} // namespace NEO
