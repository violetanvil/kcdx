cmake_minimum_required(VERSION 3.16)
project(cap-111 CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED KCDX_INCLUDE_DIR)
    set(KCDX_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
endif()
# The engine's pak_reader lives at repo-root/src/fs_takeover and the vendored
# miniz at repo-root/vendor/miniz; compile BOTH into this plugin DLL (the
# cap-110 shape + cap-109's miniz.c — a test plugin builds standalone against
# include/ + the engine source under test + the vendored inflater, never
# linking the engine). pak_reader.cpp's read path inflates a DEFLATE entry via
# miniz, so this plugin (unlike cap-110) adds miniz.c. pak_reader.cpp logs
# through the engine's kcdx::log API, whose symbols are supplied standalone by
# pak_reader_log_stub.cpp so pak_reader.cpp stays byte-identical between the
# engine and this test build.
set(FS_TAKEOVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/fs_takeover")
set(MINIZ_DIR       "${CMAKE_CURRENT_LIST_DIR}/../../vendor/miniz")
add_library(cap-111 SHARED
    cap-111.cpp
    "${FS_TAKEOVER_DIR}/pak_reader.cpp"
    "${MINIZ_DIR}/miniz.c"
    pak_reader_log_stub.cpp)
target_include_directories(cap-111 PRIVATE
    "${KCDX_INCLUDE_DIR}" "${FS_TAKEOVER_DIR}" "${MINIZ_DIR}")
if(MSVC)
    # Scope the windows-lean defines to the TUs that pull <windows.h> only.
    # miniz.c #defines WIN32_LEAN_AND_MEAN itself; passing it target-wide
    # collides with that self-define (C4005). cap-111.cpp gets all three for its
    # <windows.h>; pak_reader.cpp and the stub only need _CRT_SECURE_NO_WARNINGS
    # (their _wfopen_s/snprintf use).
    set_source_files_properties(cap-111.cpp PROPERTIES COMPILE_DEFINITIONS
        "_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN")
    set_source_files_properties("${FS_TAKEOVER_DIR}/pak_reader.cpp" PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    set_source_files_properties(pak_reader_log_stub.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
    target_compile_options(cap-111 PRIVATE /W3 /MT)
    set_property(TARGET cap-111 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set_target_properties(cap-111 PROPERTIES PREFIX "" OUTPUT_NAME "cap-111")
