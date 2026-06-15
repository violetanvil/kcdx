cmake_minimum_required(VERSION 3.16)
project(cap-110 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's pak_reader lives at repo-root/src/fs_takeover; compile its .cpp
# into this plugin DLL (the cap-109 shape — a test plugin builds standalone
# against include/ + the one engine source under test, never linking the
# engine). pak_reader.cpp logs through the engine's kcdx::log API, whose symbols
# are supplied standalone by pak_reader_log_stub.cpp so pak_reader.cpp stays
# byte-identical between the engine and this test build.
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
add_library(cap-110 SHARED
    cap-110.cpp
    "${FS_TAKEOVER_DIR}/pak_reader.cpp"
    pak_reader_log_stub.cpp)
target_include_directories(cap-110 PRIVATE "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}")
if(MSVC)
    # The windows-lean defines are needed by cap-110.cpp's <windows.h>. Scope
    # them per-file (matching cap-109's WIN32_LEAN_AND_MEAN C4005 lesson) rather
    # than target-wide, so a future TU that self-#defines any of them does not
    # collide. _CRT_SECURE_NO_WARNINGS also covers the snprintf/_wfopen_s use in
    # cap-110.cpp, pak_reader.cpp, and the stub.
    set_source_files_properties(cap-110.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/pak_reader.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties(pak_reader_log_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-110 PRIVATE /W3 /MT)
    set_property(TARGET cap-110 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-110 PROPERTIES PREFIX "" OUTPUT_NAME "cap-110")
