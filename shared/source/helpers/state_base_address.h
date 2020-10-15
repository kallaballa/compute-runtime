/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace NEO {

class GmmHelper;
class IndirectHeap;
class LinearStream;
struct DispatchFlags;

template <typename GfxFamily>
struct StateBaseAddressHelper {
    using STATE_BASE_ADDRESS = typename GfxFamily::STATE_BASE_ADDRESS;

    static void programStateBaseAddress(
        STATE_BASE_ADDRESS *stateBaseAddress,
        const IndirectHeap *dsh,
        const IndirectHeap *ioh,
        const IndirectHeap *ssh,
        uint64_t generalStateBase,
        bool setGeneralStateBaseAddress,
        uint32_t statelessMocsIndex,
        uint64_t indirectObjectHeapBaseAddress,
        uint64_t instructionHeapBaseAddress,
        bool setInstructionStateBaseAddress,
        GmmHelper *gmmHelper,
        bool isMultiOsContextCapable);

    static void appendStateBaseAddressParameters(
        STATE_BASE_ADDRESS *stateBaseAddress,
        const IndirectHeap *ssh,
        bool setGeneralStateBaseAddress,
        uint64_t indirectObjectHeapBaseAddress,
        GmmHelper *gmmHelper,
        bool isMultiOsContextCapable);

    static void programBindingTableBaseAddress(LinearStream &commandStream, const IndirectHeap &ssh, GmmHelper *gmmHelper);
};
} // namespace NEO
