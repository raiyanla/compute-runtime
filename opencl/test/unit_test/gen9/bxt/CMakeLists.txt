#
# Copyright (C) 2018-2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

if(TESTS_BXT)
  set(IGDRCL_SRCS_tests_gen9_bxt
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/device_tests_bxt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_device_caps_bxt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hw_info_config_bxt.cpp
  )
  target_sources(igdrcl_tests PRIVATE ${IGDRCL_SRCS_tests_gen9_bxt})
  add_subdirectories()
  set(unit_test_config "bxt/1/3/6")
  include(${OPENCL_UNIT_TEST_DIR}/cmake/run_ult_target.cmake)
endif()
