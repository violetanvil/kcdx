cmake_minimum_required(VERSION 3.16)
project(cap-89 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
add_library(cap-89 SHARED cap-89.cpp)
target_include_directories(cap-89 PRIVATE "${KCDX_INCLUDE_DIR}")
if(MSVC)
    target_compile_definitions(cap-89 PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(cap-89 PRIVATE /W3 /MT)
    set_property(TARGET cap-89 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

    # Emit a sidecar .pdb beside build/Release/cap-89.dll. This is the input
    # under test: the probe checks whether DbgHelp can recover a NON-EXPORTED
    # internal function's name + address from THIS .pdb. /Zi gives full debug
    # info at compile; /DEBUG:FULL links a SELF-CONTAINED .pdb that copies ALL
    # private symbol information out of the object files into the single PDB, so
    # the deployed PDB carries the plugin's own internals with NO dependency on
    # the build machine's object files. This is the variable under test: a bare
    # /DEBUG can be downgraded to /DEBUG:FASTLINK (the VS2017+ default), which
    # produces a stub PDB that only INDEXES the build-machine OBJs and carries
    # none of the plugin's private symbols when deployed. /OPT:REF + /OPT:ICF
    # undo the /DEBUG default of disabling those optimizations so the DLL is not
    # bloated by enabling debug info (the same belt the engine target uses).
    # This flag is local to THIS plugin's target — it does not touch any other
    # test plugin's build (each test plugin has its own CMakeLists; the others
    # do not emit a .pdb).
    target_compile_options(cap-89 PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(cap-89 PRIVATE
        $<$<CONFIG:Release>:/DEBUG:FULL>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
    # Belt-and-suspenders: if any inherited config flag injects /DEBUG:FASTLINK,
    # strip it so it cannot override the explicit /DEBUG:FULL above. The MSVC
    # linker takes the LAST /DEBUG form on the command line, so a stray
    # FASTLINK from a *_RELEASE flag var must not survive.
    string(REGEX REPLACE "[/-]DEBUG:FASTLINK" "" CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}")
    string(REGEX REPLACE "[/-]DEBUG:FASTLINK" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
endif()
set_target_properties(cap-89 PROPERTIES PREFIX "" OUTPUT_NAME "cap-89")
