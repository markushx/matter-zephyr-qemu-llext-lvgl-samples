# SPDX-License-Identifier: Apache-2.0
# Standalone build for the "temp_sensor" LLEXT extension, using the EDK produced
# by `west build -t llext-edk` in the main app's build directory. Modeled on
# zephyr/samples/subsys/llext/edk/ext1/CMakeLists.txt.
#
# Usage:
#   export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
#   cmake -B build -DLLEXT_EDK_INSTALL_DIR=<path-to-extracted-edk>/llext-edk
#   make -C build

cmake_minimum_required(VERSION 3.28.0)

set(CMAKE_TOOLCHAIN_FILE toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

project(temp_sensor)

if(NOT DEFINED LLEXT_EDK_INSTALL_DIR)
    set(LLEXT_EDK_INSTALL_DIR $ENV{LLEXT_EDK_INSTALL_DIR})
endif()
include(${LLEXT_EDK_INSTALL_DIR}/cmake.cflags)

add_compile_options(${LLEXT_CFLAGS})
add_compile_options("-c")
# matter_dynamic_extension.h: the app's contract with extensions.
add_compile_options(-I${PROJECT_SOURCE_DIR}/../../include)

get_property(COMPILE_OPTIONS_PROP DIRECTORY PROPERTY COMPILE_OPTIONS)

add_custom_command(
    OUTPUT
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.llext
    COMMAND ${CMAKE_C_COMPILER} ${COMPILE_OPTIONS_PROP}
        -o ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.llext
        ${PROJECT_SOURCE_DIR}/src/temp_sensor.c
)

add_custom_target(temp_sensor ALL DEPENDS ${PROJECT_BINARY_DIR}/temp_sensor.llext)
