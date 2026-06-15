cmake_minimum_required(VERSION 3.16)
project(cap-109 CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The vendored miniz lives at repo-root/vendor/miniz; compile it into this
# plugin DLL so the inflate API is available without linking the engine's
# `miniz` static lib (a test plugin builds standalone, against include/ only).
set(MINIZ_DIR "${CMAKE_CURRENT_LIST_DIR}/../../vendor/miniz")
add_library(cap-109 SHARED cap-109.cpp "${MINIZ_DIR}/miniz.c")
target_include_directories(cap-109 PRIVATE "${KCDX_INCLUDE_DIR}" "${MINIZ_DIR}")
if(MSVC)
    # Scope the windows-lean defines to cap-109.cpp only. miniz.c #defines
    # WIN32_LEAN_AND_MEAN itself; passing it target-wide collides with that
    # self-define (C4005). cap-109.cpp still gets all three for its <windows.h>.
    set_source_files_properties(cap-109.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    target_compile_options(cap-109 PRIVATE /W3 /MT)
    set_property(TARGET cap-109 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-109 PROPERTIES PREFIX "" OUTPUT_NAME "cap-109")
