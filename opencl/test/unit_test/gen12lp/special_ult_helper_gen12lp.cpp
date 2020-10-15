/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/gen12lp/special_ult_helper_gen12lp.h"

#include "shared/source/helpers/hw_info.h"

namespace NEO {

bool SpecialUltHelperGen12lp::additionalCoherencyCheck(PRODUCT_FAMILY productFamily, bool coherency) {
    return false;
}

bool SpecialUltHelperGen12lp::shouldPerformimagePitchAlignment(PRODUCT_FAMILY productFamily) {
    if (productFamily == IGFX_ROCKETLAKE) {
        return false;
    }

    return true;
}

bool SpecialUltHelperGen12lp::shouldTestDefaultImplementationOfSetupHardwareCapabilities(PRODUCT_FAMILY productFamily) {
    return false;
}

bool SpecialUltHelperGen12lp::isPipeControlWArequired(PRODUCT_FAMILY productFamily) {
    if (productFamily == IGFX_ROCKETLAKE) {
        return false;
    }

    return true;
}

} // namespace NEO
