#
# Copyright (C) 2020-2021 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

  set(IGDRCL_SRCS_mock_opengl
      ${CMAKE_CURRENT_SOURCE_DIR}/mock_opengl.cpp
      ${NEO_SOURCE_DIR}/opencl/test/unit_test/helpers/windows/mock_function.cpp
  )

  add_library(mock_opengl SHARED ${IGDRCL_SRCS_mock_opengl})
  add_dependencies(unit_tests mock_opengl)
  add_dependencies(igdrcl_tests mock_opengl)
  set_target_properties(mock_opengl PROPERTIES FOLDER "test mocks")
  target_include_directories(mock_opengl PRIVATE $<TARGET_PROPERTY:${NEO_MOCKABLE_LIB_NAME},INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(mock_opengl PRIVATE $<TARGET_PROPERTY:${NEO_MOCKABLE_LIB_NAME},INTERFACE_COMPILE_DEFINITIONS>)

