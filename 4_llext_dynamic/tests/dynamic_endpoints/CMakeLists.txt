# ztest suite for the 4_llext_dynamic/ sample: boots the sample's real Matter
# server (data model, Ember, cluster servers -- everything but a controller on
# the other end), loads the sample extensions from ../../extensions through
# the ExtensionLoaderCluster, and checks the endpoints they add by reading,
# writing and invoking through the data model provider, the same entry points
# the Interaction Model engine uses.
# Runs under plain QEMU without sudo:
#   scripts/run-llext-ztest.sh 4_llext_dynamic/tests/dynamic_endpoints
cmake_minimum_required(VERSION 3.20.0)

if(NOT CHIP_ROOT)
    if(DEFINED ENV{CHIP_ROOT})
        set(CHIP_ROOT $ENV{CHIP_ROOT})
    else()
        get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../connectedhomeip-worktree-zephyr REALPATH)
    endif()
endif()

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/zephyr/chip-module)

# Same configuration as the 4_llext_dynamic/ sample itself, plus ztest on top.
get_filename_component(SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. REALPATH)
set(CONF_FILE
    ${SAMPLE_DIR}/prj.conf
    ${SAMPLE_DIR}/boards/qemu_x86.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf
)
set(DTC_OVERLAY_FILE ${SAMPLE_DIR}/boards/qemu_x86.overlay)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(matter_zephyr_qemu_llext_dynamic_test)

get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    ${SAMPLE_DIR}/include
    ${SAMPLE_DIR}/src
    ${GEN_DIR}/app-common
    ${GEN_DIR}/bridge-app
)

# The whole sample except its main(): the test's Setup() calls AppTask::Init()
# instead.
target_sources(app PRIVATE
    src/main.cpp
    ${SAMPLE_DIR}/src/AppTask.cpp
    ${SAMPLE_DIR}/src/ActionsStub.cpp
    ${SAMPLE_DIR}/src/ZclCallbacks.cpp
    ${SAMPLE_DIR}/src/dynamic/DynamicEndpointManager.cpp
    ${SAMPLE_DIR}/src/dynamic/DynamicExports.c
    ${SAMPLE_DIR}/src/llext/ExtensionLoaderCluster.cpp
    # CHIP's own helpers for capturing what a read encodes and what a command
    # handler answers.
    ${CHIP_ROOT}/src/app/data-model-provider/tests/ReadTesting.cpp
    ${CHIP_ROOT}/src/app/server-cluster/testing/MockCommandHandler.cpp
)

chip_configure_data_model(app
    ZAP_FILE ${CHIP_ROOT}/examples/bridge-app/bridge-common/bridge-app.zap
)

# Build the sample extensions from the same sources the standalone EDK
# builds use, and embed them in the test image.
foreach(ext_name onoff_light temp_sensor counter)
    set(ext_bin ${PROJECT_BINARY_DIR}/llext/${ext_name}.llext)
    set(ext_inc ${ZEPHYR_BINARY_DIR}/include/generated/${ext_name}.inc)
    add_llext_target(${ext_name}_ext
        OUTPUT ${ext_bin}
        SOURCES ${SAMPLE_DIR}/extensions/${ext_name}/src/${ext_name}.c
    )
    # matter_dynamic_extension.h: the app's contract with extensions.
    llext_include_directories(${ext_name}_ext PRIVATE ${SAMPLE_DIR}/include)
    generate_inc_file_for_target(app ${ext_bin} ${ext_inc})
endforeach()
