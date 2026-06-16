cmake_minimum_required(VERSION 3.16)
project(cap-114 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's asset_index lives at repo-root/src/fs_takeover; compile its .cpp
# into this plugin DLL (the cap-110 shape — a test plugin builds standalone
# against include/ + the one engine source under test, never linking the
# engine). asset_index.cpp references asset_overlay::NormalizeVPath /
# GetOverlayMap, ParsePakCentralDirectory, and the kcdx::log API; those symbols
# are supplied standalone by asset_index_deps_stub.cpp so asset_index.cpp stays
# byte-identical between the engine and this test build. The SRC_DIR include is
# for src/asset_overlay.h + src/log.h (the stub + the test reference them).
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src")
add_library(cap-114 SHARED
    cap-114.cpp
    "${FS_TAKEOVER_DIR}/asset_index.cpp"
    asset_index_deps_stub.cpp)
target_include_directories(cap-114 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}" "${SRC_DIR}")
if(MSVC)
    # Per-file windows-lean defines (matching cap-110's scoping) for cap-114.cpp's
    # <windows.h>. _CRT_SECURE_NO_WARNINGS covers the snprintf/_wfopen-class use
    # across the three TUs.
    set_source_files_properties(cap-114.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/asset_index.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties(asset_index_deps_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-114 PRIVATE /W3 /MT)
    set_property(TARGET cap-114 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-114 PROPERTIES PREFIX "" OUTPUT_NAME "cap-114")
