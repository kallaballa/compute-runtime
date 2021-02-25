/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/test/unit_tests/mocks/mock_built_ins.h"

#include "shared/source/built_ins/sip.h"

namespace L0 {
namespace ult {

const NEO::SipKernel &MockBuiltins::getSipKernel(NEO::SipKernelType type, NEO::Device &device) {
    if (!(sipKernel && sipKernel->getType() == type)) {
        sipKernel.reset(new NEO::SipKernel(type, allocation.get()));
    }

    return *sipKernel;
}

} // namespace ult
} // namespace L0
