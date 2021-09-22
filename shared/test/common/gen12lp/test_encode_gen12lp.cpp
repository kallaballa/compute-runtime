/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/stream_properties.h"
#include "shared/test/common/helpers/default_hw_info.h"

#include "test.h"

#include "hw_cmds.h"

using namespace NEO;

using CommandEncodeGen12LpTest = ::testing::Test;

GEN12LPTEST_F(CommandEncodeGen12LpTest, whenProgrammingStateComputeModeThenProperFieldsAreSet) {
    using STATE_COMPUTE_MODE = typename FamilyType::STATE_COMPUTE_MODE;
    uint8_t buffer[64]{};

    StateComputeModeProperties properties;
    auto pLinearStream = std::make_unique<LinearStream>(buffer, sizeof(buffer));
    EncodeComputeMode<FamilyType>::adjustComputeMode(*pLinearStream, nullptr, properties, *defaultHwInfo);
    auto pScm = reinterpret_cast<STATE_COMPUTE_MODE *>(pLinearStream->getCpuBase());
    EXPECT_EQ(0u, pScm->getMaskBits());
    EXPECT_EQ(STATE_COMPUTE_MODE::FORCE_NON_COHERENT_FORCE_DISABLED, pScm->getForceNonCoherent());

    properties.isCoherencyRequired.value = 0;
    pLinearStream = std::make_unique<LinearStream>(buffer, sizeof(buffer));
    EncodeComputeMode<FamilyType>::adjustComputeMode(*pLinearStream, nullptr, properties, *defaultHwInfo);
    pScm = reinterpret_cast<STATE_COMPUTE_MODE *>(pLinearStream->getCpuBase());
    EXPECT_EQ(0u, pScm->getMaskBits());
    EXPECT_EQ(STATE_COMPUTE_MODE::FORCE_NON_COHERENT_FORCE_DISABLED, pScm->getForceNonCoherent());

    properties.isCoherencyRequired.isDirty = true;
    pLinearStream = std::make_unique<LinearStream>(buffer, sizeof(buffer));
    EncodeComputeMode<FamilyType>::adjustComputeMode(*pLinearStream, nullptr, properties, *defaultHwInfo);
    pScm = reinterpret_cast<STATE_COMPUTE_MODE *>(pLinearStream->getCpuBase());
    EXPECT_EQ(FamilyType::stateComputeModeForceNonCoherentMask, pScm->getMaskBits());
    EXPECT_EQ(STATE_COMPUTE_MODE::FORCE_NON_COHERENT_FORCE_GPU_NON_COHERENT, pScm->getForceNonCoherent());
}
