cmake_minimum_required(VERSION 3.16)
project(comp-14 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
add_library(comp-14 SHARED comp-14.cpp)
target_include_directories(comp-14 PRIVATE "${KCDX_INCLUDE_DIR}")
if(MSVC)
    target_compile_definitions(comp-14 PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(comp-14 PRIVATE /W3 /MT)
    set_property(TARGET comp-14 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    # /OPT:NOICF — keep the stub target at its own VA. Identical-COMDAT-
    # folding could otherwise merge Comp14_Target with a byte-identical
    # function elsewhere and break the address-locator -> chain-targetVa
    # identity this row proves (the cap-20 / cap-36 lesson). The volatile
    # tag in comp-14.cpp also defeats ICF; belt-and-suspenders.
    target_link_options(comp-14 PRIVATE /OPT:NOICF)
endif()
set_target_properties(comp-14 PROPERTIES PREFIX "" OUTPUT_NAME "comp-14")
