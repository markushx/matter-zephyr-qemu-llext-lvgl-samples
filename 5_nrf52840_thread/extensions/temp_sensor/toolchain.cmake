# SPDX-License-Identifier: Apache-2.0
# Modeled on zephyr/samples/subsys/llext/edk/ext1/toolchain.cmake.

set(CMAKE_C_COMPILER arm-zephyr-eabi-gcc)
set(CMAKE_FIND_ROOT_PATH $ENV{ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi)
