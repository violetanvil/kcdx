# Standalone CMakeLists for the hello-plugin example.
#
# This is a deliberate "out-of-tree" example — you can copy this whole folder
# anywhere, point cmake at the kcdx include dir, and build. Demonstrates the
# minimum a plugin author needs: the public header is the only kcdx
# dependency.
#
# Build from this folder:
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
#       -DKCDX_INCLUDE_DIR="C:/path/to/kcdx/include"
#   cmake --build build --config Release
#
# Output: build/Release/hello-plugin.dll. Drop in <game>/plugins/hello-plugin/
# alongside an installed kcdx.asi.

cmake_minimum_required(VERSION 3.16)
project(hello-plugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Default to the in-tree include dir (assumes this example sits in
# kcdx/examples/hello-plugin/). Override with -DKCDX_INCLUDE_DIR=... when
# building from a copy of this folder somewhere else.
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()

add_library(hello-plugin SHARED hello-plugin.cpp)
target_include_directories(hello-plugin PRIVATE "${KCDX_INCLUDE_DIR}")

if(MSVC)
    target_compile_definitions(hello-plugin PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(hello-plugin PRIVATE /W3 /MT)
    set_property(TARGET hello-plugin PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Standard .dll output (NOT .asi — plugin DLLs are normal DLLs).
set_target_properties(hello-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hello-plugin"
)
