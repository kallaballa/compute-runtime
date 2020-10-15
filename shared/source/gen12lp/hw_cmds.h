/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/gen12lp/hw_cmds_base.h"
#ifdef SUPPORT_TGLLP
#include "hw_cmds_tgllp.h"
#endif
#ifdef SUPPORT_DG1
#include "hw_cmds_dg1.h"
#endif
#ifdef SUPPORT_RKL
#include "hw_cmds_rkl.h"
#endif
