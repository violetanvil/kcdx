cmake_minimum_required(VERSION 3.16)
project(cap-116 CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's file-handle pool lives at repo-root/src/fs_takeover/file_handle.cpp.
# Compile it into this plugin DLL (the cap-112/115 shape — a test plugin builds
# standalone against include/ + the engine source under test, never linking the
# engine). file_handle.cpp is self-contained (no boot_trace dependency — it both
# declares and defines the PROBE P-read tag symbols itself) and logs through the
# engine kcdx::log API; log_stub.cpp supplies those symbols, keeping file_handle.cpp
# BYTE-IDENTICAL between the engine and this build.
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
set(SRC_DIR         "${CMAKE_CURRENT_LIST_DIR}/../../src")
add_library(cap-116 SHARED
    cap-116.cpp
    "${FS_TAKEOVER_DIR}/file_handle.cpp"
    log_stub.cpp)
target_include_directories(cap-116 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}" "${SRC_DIR}")
if(MSVC)
    # cap-116.cpp pulls <windows.h> → all three lean defines; the engine TU + stub
    # need only _CRT_SECURE_NO_WARNINGS (file_handle.cpp's _wfopen_s/_fseeki64 use,
    # the stub's vsnprintf). file_handle.cpp also pulls <windows.h> for GetFileTime,
    # so it gets the lean defines too (no miniz self-define collision here — no miniz).
    set_source_files_properties(cap-116.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/file_handle.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties(log_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-116 PRIVATE /W3 /MT)
    set_property(TARGET cap-116 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-116 PROPERTIES PREFIX "" OUTPUT_NAME "cap-116")
