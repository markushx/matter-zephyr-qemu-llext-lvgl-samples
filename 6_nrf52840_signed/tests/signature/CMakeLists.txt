# ztest suite for ExtensionSignature, with CHIP's real crypto. The sample itself only
# builds for the DK, with NCS, so this uses the configuration of the 2_llext/ sample,
# which has the same PSA crypto backend, and only ExtensionSignature.cpp from this one.
# Runs under plain QEMU without sudo:
#   scripts/run-llext-ztest.sh 6_nrf52840_signed/tests/signature
cmake_minimum_required(VERSION 3.20.0)

if(NOT CHIP_ROOT)
    if(DEFINED ENV{CHIP_ROOT})
        set(CHIP_ROOT $ENV{CHIP_ROOT})
    else()
        get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../connectedhomeip-worktree-zephyr REALPATH)
    endif()
endif()

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/zephyr/chip-module)

get_filename_component(SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. REALPATH)
get_filename_component(CONFIG_DIR ${SAMPLE_DIR}/../2_llext REALPATH)
set(CONF_FILE
    ${CONFIG_DIR}/prj.conf
    ${CONFIG_DIR}/boards/qemu_x86.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf
)
set(DTC_OVERLAY_FILE ${CONFIG_DIR}/boards/qemu_x86.overlay)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(matter_nrf52840_signed_test)

target_include_directories(app PRIVATE
    ${CONFIG_DIR}/include
    ${SAMPLE_DIR}/src
)

target_sources(app PRIVATE
    src/main.cpp
    ${SAMPLE_DIR}/src/llext/ExtensionSignature.cpp
)
