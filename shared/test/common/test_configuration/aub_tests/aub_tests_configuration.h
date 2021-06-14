/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

struct AubTestsConfig {
    bool testCanonicalAddress;
};

template <typename GfxFamily>
AubTestsConfig GetAubTestsConfig();
