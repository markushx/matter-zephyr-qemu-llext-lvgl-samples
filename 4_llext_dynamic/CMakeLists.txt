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
project(matter_zephyr_qemu_llext_dynamic)

get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    include
    ${GEN_DIR}/app-common
    ${GEN_DIR}/bridge-app
)

target_sources(app PRIVATE
    src/main.cpp
    src/AppTask.cpp
    src/ActionsStub.cpp
    src/ZclCallbacks.cpp
    src/dynamic/DynamicEndpointManager.cpp
    src/dynamic/DynamicExports.c
    src/llext/ExtensionLoaderCluster.cpp
)

chip_configure_data_model(app
    # The bridge data model: root node (EP0), Aggregator (EP1), and a
    # placeholder endpoint (EP2) that only makes ZAP generate the cluster
    # servers bridged devices can use -- DynamicEndpointManager::Init() hides it.
    ZAP_FILE ${CHIP_ROOT}/examples/bridge-app/bridge-common/bridge-app.zap
)
