cmake_minimum_required(VERSION 3.16)
project(cap-37 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
add_library(cap-37 SHARED cap-37.cpp)
target_include_directories(cap-37 PRIVATE "${KCDX_INCLUDE_DIR}")
if(MSVC)
    target_compile_definitions(cap-37 PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(cap-37 PRIVATE /W3 /MT)
    set_property(TARGET cap-37 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    # /OPT:NOICF — keep the per-row stub functions at DISTINCT addresses.
    # Identical-COMDAT-folding would otherwise merge byte-identical stubs to
    # one VA and collapse the rows' separate hook chains into one (the cap-20
    # lesson; cap-36 carries the same flag). The wrapper's Before<Sig,&fn>
    # also generates a distinct static adapter per (Sig, fn) pair, but the
    # raw stub VAs still need to be distinct — the per-stub unique tag in
    # cap-37.cpp defeats ICF too; this is belt-and-suspenders.
    target_link_options(cap-37 PRIVATE /OPT:NOICF)
endif()
set_target_properties(cap-37 PROPERTIES PREFIX "" OUTPUT_NAME "cap-37")
