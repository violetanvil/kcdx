cmake_minimum_required(VERSION 3.16)
project(cap-117 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's loose-open mode sanitizer lives at
# repo-root/src/fs_takeover/loose_mode.cpp. Compile it into this plugin DLL (the
# cap-112/115/116 shape — a test plugin builds standalone against include/ + the
# engine source under test, never linking the engine). loose_mode.cpp is pure +
# self-contained (only <string>/<cstring> — no logging, so NO log stub is needed),
# byte-identical between the engine and this build.
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
add_library(cap-117 SHARED
    cap-117.cpp
    "${FS_TAKEOVER_DIR}/loose_mode.cpp")
target_include_directories(cap-117 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}")
if(MSVC)
    set_source_files_properties(cap-117.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/loose_mode.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-117 PRIVATE /W3 /MT)
    set_property(TARGET cap-117 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-117 PROPERTIES PREFIX "" OUTPUT_NAME "cap-117")
