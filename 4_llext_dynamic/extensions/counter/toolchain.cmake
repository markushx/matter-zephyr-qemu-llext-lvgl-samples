# SPDX-License-Identifier: Apache-2.0
# Modeled on zephyr/samples/subsys/llext/edk/ext1/toolchain.cmake, adapted
# for x86 (qemu_x86 uses -m32 via the EDK's own captured cflags, not a
# dedicated 32-bit-named toolchain binary).

set(CMAKE_C_COMPILER x86_64-zephyr-elf-gcc)
set(CMAKE_FIND_ROOT_PATH $ENV{ZEPHYR_SDK_INSTALL_DIR}/x86_64-zephyr-elf)
