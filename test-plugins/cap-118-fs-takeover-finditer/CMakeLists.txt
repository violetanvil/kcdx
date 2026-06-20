cmake_minimum_required(VERSION 3.16)
project(cap-118 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's find_slots lives at repo-root/src/fs_takeover; compile its .cpp
# into this plugin DLL (the cap-114 shape — a test plugin builds standalone
# against include/ + the one engine source under test, never linking the engine).
# find_slots.cpp references asset_overlay::NormalizeVPath (real, the test fold),
# the fs_takeover handle-pool find ops + kcdx_AdjustFileName + GetBuiltIndex (only
# the slot impls reach these — link-completeness stubs), init::Current (only
# through boot_trace's BootWindowActive — stub), and the kcdx::log API; those
# symbols are supplied standalone by find_slots_deps_stub.cpp so find_slots.cpp
# stays byte-identical between the engine and this test build. The SRC_DIR include
# is for src/asset_overlay.h + src/init_phase.h + src/log.h (the stub references
# them).
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src")
add_library(cap-118 SHARED
    cap-118.cpp
    "${FS_TAKEOVER_DIR}/find_slots.cpp"
    find_slots_deps_stub.cpp)
target_include_directories(cap-118 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}" "${SRC_DIR}")
if(MSVC)
    # Per-file windows-lean defines (matching cap-114's scoping) for cap-118.cpp's
    # + find_slots.cpp's <windows.h>. _CRT_SECURE_NO_WARNINGS covers the
    # snprintf/_wfindfirst64-class use across the three TUs.
    set_source_files_properties(cap-118.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/find_slots.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties(find_slots_deps_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-118 PRIVATE /W3 /MT)
    set_property(TARGET cap-118 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-118 PROPERTIES PREFIX "" OUTPUT_NAME "cap-118")
