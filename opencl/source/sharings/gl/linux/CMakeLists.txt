#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

set(RUNTIME_SRCS_SHARINGS_GL_LINUX
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/cl_gl_api.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/include/gl_types.h
      ${CMAKE_CURRENT_SOURCE_DIR}/lin_enable_gl.h
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_arb_sync_event_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_buffer_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_context_guard_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_library_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_sharing_linux.h
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_sharing_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_sync_event_linux.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/gl_texture_linux.cpp
   )
      target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${RUNTIME_SRCS_SHARINGS_GL_LINUX})

      set(RUNTIME_SRCS_SHARINGS_GL_ENABLE_LINUX
          ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
          ${CMAKE_CURRENT_SOURCE_DIR}/lin_enable_gl.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/lin_enable_gl.h
      )
  target_sources(${SHARINGS_ENABLE_LIB_NAME} PRIVATE ${RUNTIME_SRCS_SHARINGS_GL_ENABLE_LINUX})
  set_property(GLOBAL PROPERTY RUNTIME_SRCS_SHARINGS_GL_LINUX ${RUNTIME_SRCS_SHARINGS_GL_LINUX})
  set_property(GLOBAL PROPERTY RUNTIME_SRCS_SHARINGS_GL_ENABLE_LINUX ${RUNTIME_SRCS_SHARINGS_GL_ENABLE_LINUX})
