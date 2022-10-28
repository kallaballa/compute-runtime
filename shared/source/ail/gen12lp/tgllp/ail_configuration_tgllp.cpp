/*
 * Copyright (C) 2021-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/ail/ail_configuration.h"
#include "shared/source/ail/ail_configuration_base.inl"

#include <map>

namespace NEO {
static EnableAIL<IGFX_TIGERLAKE_LP> enableAILTGLLP;

std::map<std::string_view, std::vector<AILEnumeration>> applicationMapTGLLP = {};

template class AILConfigurationHw<IGFX_TIGERLAKE_LP>;

} // namespace NEO
