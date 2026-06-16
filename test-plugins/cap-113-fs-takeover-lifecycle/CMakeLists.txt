cmake_minimum_required(VERSION 3.16)
project(cap-113 CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's open/read slot impls live at repo-root/src/fs_takeover; they
# compose the file-handle pool (same dir), the unified asset index + the pak
# reader (same dir), the asset-overlay seam (repo-root/src), and the vendored
# miniz inflater. Compile the slot impls + the index + the reader + miniz into
# this plugin DLL (the cap-112 shape — a test plugin builds standalone against
# include/ + the engine source under test + the vendored inflater, never linking
# the engine), so the test drives the ACTUAL kcdx slot impls end-to-end. The
# engine TUs log through the engine kcdx::log API (symbols from log_stub.cpp) and
# the index composes the asset-overlay seam (NormalizeVPath + GetOverlayMap from
# asset_overlay_seam_stub.cpp, which also exposes a test-only SetTestOverlayMap
# driver) — both stubs keep every engine TU BYTE-IDENTICAL between the engine and
# this build (no test carve-out in production source; the stubs supply the seams).
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
set(SRC_DIR         "${CMAKE_CURRENT_LIST_DIR}/../../src")
set(MINIZ_DIR       "${CMAKE_CURRENT_LIST_DIR}/../../vendor/miniz")
add_library(cap-113 SHARED
    cap-113.cpp
    "${FS_TAKEOVER_DIR}/open_slots.cpp"
    "${FS_TAKEOVER_DIR}/read_slots.cpp"
    "${FS_TAKEOVER_DIR}/file_handle.cpp"
    "${FS_TAKEOVER_DIR}/asset_index.cpp"
    "${FS_TAKEOVER_DIR}/pak_reader.cpp"
    "${MINIZ_DIR}/miniz.c"
    log_stub.cpp
    asset_overlay_seam_stub.cpp)
target_include_directories(cap-113 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}" "${SRC_DIR}" "${MINIZ_DIR}")
if(MSVC)
    # Scope the windows-lean defines to the TUs that pull <windows.h> only.
    # miniz.c #defines WIN32_LEAN_AND_MEAN itself; passing it target-wide collides
    # with that self-define (C4005). cap-113.cpp gets all three for its
    # <windows.h>; the engine TUs + stubs only need _CRT_SECURE_NO_WARNINGS (their
    # _wfopen_s/snprintf/fread use).
    set_source_files_properties(cap-113.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/open_slots.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties("${FS_TAKEOVER_DIR}/read_slots.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties("${FS_TAKEOVER_DIR}/file_handle.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties("${FS_TAKEOVER_DIR}/asset_index.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties("${FS_TAKEOVER_DIR}/pak_reader.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties(log_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties(asset_overlay_seam_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-113 PRIVATE /W3 /MT)
    set_property(TARGET cap-113 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-113 PROPERTIES PREFIX "" OUTPUT_NAME "cap-113")
