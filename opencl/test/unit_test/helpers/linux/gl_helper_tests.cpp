/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/os_library.h"
#include "shared/source/os_interface/linux/linux_wrapper.h"
#include "shared/test/common/test_macros/test.h"

#include "opencl/source/helpers/linux/gl_helper.h"
#include "opencl/test/unit_test/helpers/linux/mock_function.h"

#include "gtest/gtest.h"

#include <memory>

typedef const char *(*funcType)();

namespace NEO {
class glFunctionHelperMock : public glFunctionHelper {
  public:
    glFunctionHelperMock(OsLibrary *glLibrary, const std::string &functionName) : glFunctionHelper(glLibrary, functionName) {}
    using glFunctionHelper::glFunctionPtr;
};

TEST(glFunctionHelper, whenCreateGlFunctionHelperThenSetGlFunctionPtrToLoadAnotherFunctions) {
    std::unique_ptr<OsLibrary> glLibrary(OsLibrary::load("libmock_opengl.so"));
    EXPECT_TRUE(glLibrary->isLoaded());
    glFunctionHelperMock loader(glLibrary.get(), "mockLoader");
    funcType function1 = ConvertibleProcAddr{loader.glFunctionPtr("realFunction")};
    funcType function2 = loader["realFunction"];
    EXPECT_STREQ(function1(), function2());
}

TEST(glFunctionHelper, givenNonExistingFunctionNameWhenCreateGlFunctionHelperThenNullptr) {
    std::unique_ptr<OsLibrary> glLibrary(OsLibrary::load("libmock_opengl.so"));
    EXPECT_TRUE(glLibrary->isLoaded());
    glFunctionHelper loader(glLibrary.get(), "mockLoader");
    funcType function = loader["nonExistingFunction"];
    EXPECT_EQ(nullptr, function);
}

TEST(glFunctionHelper, givenRealFunctionNameWhenCreateGlFunctionHelperThenGetPointerToAppropriateFunction) {
    std::unique_ptr<OsLibrary> glLibrary(OsLibrary::load("libmock_opengl.so"));
    EXPECT_TRUE(glLibrary->isLoaded());
    glFunctionHelper loader(glLibrary.get(), "mockLoader");
    funcType function = loader["realFunction"];
    EXPECT_STREQ(realFunction(), function());
}
}; // namespace NEO
