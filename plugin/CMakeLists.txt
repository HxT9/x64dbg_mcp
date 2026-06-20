cmake_minimum_required(VERSION 3.20)
project(x64dbg_mcp_plugin CXX)

# ---------------------------------------------------------------------------
# Locate the x64dbg source tree (for the plugin SDK headers) and the matching
# build output (for the x64dbg.lib / x64bridge.lib import libraries).
# Override on the command line with -DX64DBG_ROOT=...
# ---------------------------------------------------------------------------
set(X64DBG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../x64dbg" CACHE PATH "Path to the x64dbg source/checkout root")

if(NOT EXISTS "${X64DBG_ROOT}/src/bridge/bridgemain.h")
    message(FATAL_ERROR "X64DBG_ROOT='${X64DBG_ROOT}' does not look like an x64dbg checkout (missing src/bridge/bridgemain.h)")
endif()

# Architecture -> plugin extension + import-library directory.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLUGIN_EXT ".dp64")
    set(X64DBG_LIBDIR "${X64DBG_ROOT}/bin/x64")
else()
    set(PLUGIN_EXT ".dp32")
    set(X64DBG_LIBDIR "${X64DBG_ROOT}/bin/x32")
endif()

if(NOT EXISTS "${X64DBG_LIBDIR}/x64bridge.lib" AND NOT EXISTS "${X64DBG_LIBDIR}/x32bridge.lib")
    message(WARNING "No bridge import library found in ${X64DBG_LIBDIR}; the plugin will fail to link until x64dbg is built.")
endif()

add_library(x64dbg_mcp SHARED
    src/plugin.cpp
    src/http_server.cpp
    src/rpc.cpp
)

target_include_directories(x64dbg_mcp PRIVATE
    "${X64DBG_ROOT}/src/bridge"
    "${X64DBG_ROOT}/src/dbg"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(x64dbg_mcp PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_features(x64dbg_mcp PRIVATE cxx_std_17)

# Bridge + dbg import libraries (x64bridge.lib / x64dbg.lib, or x32 variants).
find_library(X64DBG_BRIDGE_LIB NAMES x64bridge x32bridge PATHS "${X64DBG_LIBDIR}" NO_DEFAULT_PATH)
find_library(X64DBG_DBG_LIB    NAMES x64dbg x32dbg       PATHS "${X64DBG_LIBDIR}" NO_DEFAULT_PATH)

target_link_libraries(x64dbg_mcp PRIVATE
    ${X64DBG_BRIDGE_LIB}
    ${X64DBG_DBG_LIB}
    ws2_32
)

# Produce e.g. x64dbg_mcp.dp64 with no "lib" prefix.
set_target_properties(x64dbg_mcp PROPERTIES
    PREFIX ""
    SUFFIX "${PLUGIN_EXT}"
    OUTPUT_NAME "x64dbg_mcp"
)

# Optional: copy the built plugin straight into x64dbg's plugins folder.
# Enable with -DINSTALL_TO_X64DBG=ON
option(INSTALL_TO_X64DBG "Copy the built plugin into <X64DBG_ROOT>/bin/<arch>/plugins" OFF)
if(INSTALL_TO_X64DBG)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_plugdir "${X64DBG_ROOT}/bin/x64/plugins")
    else()
        set(_plugdir "${X64DBG_ROOT}/bin/x32/plugins")
    endif()
    add_custom_command(TARGET x64dbg_mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_plugdir}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:x64dbg_mcp>" "${_plugdir}/"
        COMMENT "Installing plugin to ${_plugdir}")
endif()
