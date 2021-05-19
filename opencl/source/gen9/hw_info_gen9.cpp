/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifdef SUPPORT_BXT
#include "hw_info_bxt.inl"
#endif
#ifdef SUPPORT_CFL
#include "hw_info_cfl.inl"
#endif
#ifdef SUPPORT_GLK
#include "hw_info_glk.inl"
#endif
#ifdef SUPPORT_KBL
#include "hw_info_kbl.inl"
#endif
#ifdef SUPPORT_SKL
#include "hw_info_skl.inl"
#endif

namespace NEO {
const char *GfxFamilyMapper<IGFX_GEN9_CORE>::name = "Gen9";
} // namespace NEO
