# ztest suite for UI extensions: sends the sample UI extensions from
# ../../extensions (embedded in the test image) through the
# ExtensionLoaderCluster's command handlers directly -- no commissioning, no
# network traffic -- with the real GUI thread running, and asks the runner
# for a screenshot of each resulting UI. Runs under plain QEMU without sudo:
#   scripts/run-llext-ztest.sh 3_llext_lvgl/tests/extension_ui
cmake_minimum_required(VERSION 3.20.0)

if(NOT CHIP_ROOT)
    if(DEFINED ENV{CHIP_ROOT})
        set(CHIP_ROOT $ENV{CHIP_ROOT})
    else()
        get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../connectedhomeip-worktree-zephyr REALPATH)
    endif()
endif()

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/zephyr/chip-module)

# Same configuration as the 3_llext_lvgl/ sample itself, plus ztest on top, so the
# cluster is tested in the environment it actually ships in.
get_filename_component(SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. REALPATH)
set(CONF_FILE
    ${SAMPLE_DIR}/prj.conf
    ${SAMPLE_DIR}/boards/qemu_x86.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf
)
set(DTC_OVERLAY_FILE ${SAMPLE_DIR}/boards/qemu_x86.overlay)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(matter_zephyr_qemu_llext_lvgl_test)

get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)

target_include_directories(app PRIVATE
    ${SAMPLE_DIR}/include
    ${SAMPLE_DIR}/src
    ${GEN_DIR}/app-common
)

target_sources(app PRIVATE
    src/main.cpp
    ${SAMPLE_DIR}/src/gui/BulbUi.cpp
    ${SAMPLE_DIR}/src/gui/LvglExports.c
    ${SAMPLE_DIR}/src/llext/ExtensionLoaderCluster.cpp
    # CHIP's own helper for capturing what a cluster encodes on a read.
    ${CHIP_ROOT}/src/app/data-model-provider/tests/ReadTesting.cpp
)

# Build the sample extensions from the same sources the standalone EDK
# builds use, and embed them in the test image.
foreach(ext_name ui_bar ui_led)
    set(ext_bin ${PROJECT_BINARY_DIR}/llext/${ext_name}.llext)
    set(ext_inc ${ZEPHYR_BINARY_DIR}/include/generated/${ext_name}.inc)
    add_llext_target(${ext_name}_ext
        OUTPUT ${ext_bin}
        SOURCES ${SAMPLE_DIR}/extensions/${ext_name}/src/${ext_name}.c
    )
    # bulb_ui_extension.h: the app's contract with UI extensions.
    llext_include_directories(${ext_name}_ext PRIVATE ${SAMPLE_DIR}/include)
    generate_inc_file_for_target(app ${ext_bin} ${ext_inc})
endforeach()
