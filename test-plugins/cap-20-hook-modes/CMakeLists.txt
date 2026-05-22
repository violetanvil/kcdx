cmake_minimum_required(VERSION 3.16)
project(cap-20 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
add_library(cap-20 SHARED cap-20.cpp)
target_include_directories(cap-20 PRIVATE "${KCDX_INCLUDE_DIR}")
if(MSVC)
    target_compile_definitions(cap-20 PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(cap-20 PRIVATE /W3 /MT)
    set_property(TARGET cap-20 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    # /OPT:NOICF — keep the per-sub-test target functions at DISTINCT
    # addresses. Identical-COMDAT-folding would otherwise merge
    # byte-identical targets to one VA and collapse the test's separate
    # hook chains into one. (The per-target unique tag in cap-20.cpp also
    # defeats ICF; this is belt-and-suspenders.)
    target_link_options(cap-20 PRIVATE /OPT:NOICF)
endif()
set_target_properties(cap-20 PROPERTIES PREFIX "" OUTPUT_NAME "cap-20")
