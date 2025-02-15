#
# Copyright (C) 2020-2022 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

set(TARGET_NAME ${TARGET_NAME_L0}_fixtures)

set(L0_FIXTURES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/aub_csr_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/cmdlist_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cmdlist_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/cmdlist_fixture.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/device_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/device_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host_pointer_manager_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/module_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/memory_ipc_fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/multi_tile_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/multi_tile_fixture.h
)

add_library(${TARGET_NAME} OBJECT ${L0_FIXTURES_SOURCES} ${NEO_CORE_tests_compiler_mocks})

target_include_directories(${TARGET_NAME} PRIVATE
                           $<TARGET_PROPERTY:gmock-gtest,INTERFACE_INCLUDE_DIRECTORIES>
                           $<TARGET_PROPERTY:${L0_MOCKABLE_LIB_NAME},INTERFACE_INCLUDE_DIRECTORIES>
                           $<TARGET_PROPERTY:${NEO_SHARED_MOCKABLE_LIB_NAME},INTERFACE_INCLUDE_DIRECTORIES>
                           ${NEO_SOURCE_DIR}/level_zero/core/test/unit_test
)

target_compile_definitions(${TARGET_NAME} PRIVATE $<TARGET_PROPERTY:${L0_MOCKABLE_LIB_NAME},INTERFACE_COMPILE_DEFINITIONS>)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER ${TARGET_NAME_L0})

create_source_tree(${TARGET_NAME} ${L0_ROOT_DIR})
