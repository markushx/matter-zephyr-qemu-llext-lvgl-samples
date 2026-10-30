cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(matter_nrf52840_thread)

include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)
ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

target_include_directories(app PRIVATE
    src
    include
    ${ZAP_PARENT_DIR}
)

target_sources(app PRIVATE
    src/main.cpp
    src/app_task.cpp
    src/dynamic/DynamicEndpointManager.cpp
    src/dynamic/DynamicExports.c
    src/llext/ExtensionLoaderCluster.cpp
)

ncs_configure_data_model()

# LLEXT's table of exported syscalls declares every syscall's implementation as
# a variable, which LTO refuses to merge with the real functions ("redeclared as
# variable"). Zephyr has no option for this, so build these two without LTO.
set_source_files_properties(${ZEPHYR_BASE}/subsys/llext/llext_export.c
    TARGET_DIRECTORY subsys__llext
    PROPERTIES COMPILE_OPTIONS $<TARGET_PROPERTY:compiler,prohibit_lto>
)
set_source_files_properties(${ZEPHYR_BINARY_DIR}/syscall_weakdefs_llext.c
    TARGET_DIRECTORY syscall_weakdefs
    PROPERTIES COMPILE_OPTIONS $<TARGET_PROPERTY:compiler,prohibit_lto>
)
