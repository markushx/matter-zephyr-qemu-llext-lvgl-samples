cmake_minimum_required(VERSION 3.20.0)

if(NOT CHIP_ROOT)
    if(DEFINED ENV{CHIP_ROOT})
        set(CHIP_ROOT $ENV{CHIP_ROOT})
    else()
        get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../connectedhomeip-worktree-zephyr REALPATH)
    endif()
endif()

# CONFIG_CHIP isn't known yet (Kconfig hasn't run) -- chip-module/CMakeLists.txt
# itself gates everything on CONFIG_CHIP, so it's harmless to always add it.
list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/zephyr/chip-module)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(matter_zephyr_qemu_llext_lvgl)

get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    include
    ${GEN_DIR}/app-common
    ${GEN_DIR}/lighting-app
)

# For the LLEXT EDK (`--target llext-edk`), which UI extensions are built
# against: it ships exactly the include directories Zephyr knows about, and
# LVGL's own module only registers its src/ -- whose headers then reach into
# the sibling include/ via relative paths. Without this the EDK's <lvgl.h>
# doesn't compile.
zephyr_include_directories(${ZEPHYR_LVGL_MODULE_DIR}/include)

target_sources(app PRIVATE
    src/main.cpp
    src/AppTask.cpp
    src/LightingManager.cpp
    src/ZclCallbacks.cpp
    src/gui/BulbUi.cpp
    src/gui/LvglExports.c
    src/llext/ExtensionLoaderCluster.cpp
)

chip_configure_data_model(app
    ZAP_FILE ${CHIP_ROOT}/examples/lighting-app/lighting-common/lighting-app.zap
)
