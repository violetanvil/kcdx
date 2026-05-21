cmake_minimum_required(VERSION 3.16)
project(kcdx C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Force static MSVC runtime globally before any add_subdirectory(). Without
# this, vendored sub-projects (asmjit in particular) default to /MD and
# produce LNK2038 against kcdx's /MT. CMP0091 lets MSVC_RUNTIME_LIBRARY
# override the per-target compile flags consistently.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -----------------------------------------------------------------------------
# Lua 5.1 (vendored from CryEngine 5.2.3 SDK — same Lua the game ships)
# -----------------------------------------------------------------------------
file(GLOB LUA_SOURCES vendor/lua/*.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC vendor/lua)
if(MSVC)
    target_compile_options(lua PRIVATE /wd4244 /wd4267 /wd4334 /wd4310)
    target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# -----------------------------------------------------------------------------
# MinHook (vendored)
# -----------------------------------------------------------------------------
add_library(minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC vendor/minhook/include)
target_include_directories(minhook PRIVATE vendor/minhook/src)

# -----------------------------------------------------------------------------
# asmjit (vendored flat-copy at commit 0bd5787, Zlib license)
#
# Used by Phase 5's typed-marshaling trampolines for [[hook]] lua_callback
# and [[mid_hook]] entries. Plugin authors don't see asmjit directly —
# it's an engine-internal dep.
#
# Build options chosen to minimize the binary size impact:
#   - ASMJIT_NO_INSTALL: skip install targets (we don't ship asmjit as a
#     standalone library, just statically linked into kcdx.asi)
#   - ASMJIT_NO_FOREIGN: drop the non-x86-64 backends (KCD2 is x86-64 only).
#     Saves ~2 MB of compiled-but-unused code.
# -----------------------------------------------------------------------------
set(ASMJIT_NO_INSTALL ON CACHE BOOL "" FORCE)
set(ASMJIT_NO_FOREIGN ON CACHE BOOL "" FORCE)
set(ASMJIT_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/asmjit)

# -----------------------------------------------------------------------------
# rom-borrowed (Phase 5+: vendored RoM source files for dynamic_hook bindings)
# -----------------------------------------------------------------------------
# add_library(rom_borrowed STATIC
#     vendor/rom-borrowed/memory.cpp
#     vendor/rom-borrowed/runtime_func_t.cpp
#     vendor/rom-borrowed/asmjit_helper.cpp
#     vendor/rom-borrowed/type_info_t.cpp
# )
# target_include_directories(rom_borrowed PUBLIC vendor/rom-borrowed)
# target_link_libraries(rom_borrowed PUBLIC asmjit minhook lua)
# Uncomment when Phase 5 lands.

# -----------------------------------------------------------------------------
# kcdx.asi — the engine itself
# -----------------------------------------------------------------------------
# Source list grows phase by phase. Phase 1 ships the locator pipeline
# adapted from kcd2-mempatch (same byte-rewrite engine, kcdx.toml filename
# and kcdx:: namespace). Later phases add plugin_loader, messaging,
# trampoline, hook_engine, scripting, serialization, etc.
add_library(kcdx SHARED
    src/dllmain.cpp
    src/log.cpp
    src/crash_guard.cpp
    src/watchdog_spawn.cpp
    src/paths.cpp
    src/pe_helpers.cpp
    src/patch_engine.cpp
    src/config.cpp
    src/hooks.cpp
    src/save_load_hooks.cpp
    src/serialization.cpp
    src/address_library.cpp
    src/console.cpp
    src/lua_bind.cpp
    src/plugin_loader.cpp
    src/interfaces.cpp
    src/messaging.cpp
    src/task.cpp
    src/trampoline.cpp
    src/hook_engine.cpp
    src/symbols.cpp
    src/trampoline_engine.cpp
    src/conflict_engine.cpp
    src/load_order.cpp
    src/ldr_notify.cpp
    src/detour_hook.cpp
    src/scripting.cpp
    src/lua_memory.cpp
    src/lua_bind_helpers.cpp
    src/lua_bind_pointer.cpp
    src/lua_bind_memory.cpp
    src/lua_bind_dynamic_hook.cpp
    src/lua_bind_dynamic_call.cpp
    src/lua_bind_lua.cpp
    src/scripting_interface.cpp
    src/dev.cpp
    src/scan_engine.cpp
    src/test.cpp
    src/lua_bind_dev.cpp
    src/lua_bind_test.cpp
    src/rom_borrowed/type_info_t.cpp
    src/rom_borrowed/asmjit_helper.cpp
    src/rom_borrowed/runtime_func_t.cpp
)

target_include_directories(kcdx PRIVATE
    src
    include
    vendor/lua
    vendor/minhook/include
    vendor/minhook/src       # hde/hde64.h — used by hook_engine to
                             # auto-decode instruction length for
                             # [[mid_hook]] stack_restore_offset.
    vendor/tomlplusplus
    vendor/asmjit            # <asmjit/asmjit.h> for rom_borrowed/
)

# version.lib is needed for GetFileVersionInfo* calls in plugin_loader.cpp
# asmjit is linked even when no [[hook]] uses lua_callback yet — Phase 5
# code references it; the linker dead-strips unused asmjit code per-symbol.
target_link_libraries(kcdx PRIVATE lua minhook asmjit version)

if(MSVC)
    target_compile_definitions(kcdx PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(kcdx PRIVATE /W3 /MT)
    set_property(TARGET kcdx    PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET lua     PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

    # crash_guard.cpp uses __try/__except in a C++ TU. /EHa turns on
    # the structured-exception unwinder so SEH and C++ exceptions can
    # coexist in the same function. Applied per-file to keep the rest
    # of the binary on the cheaper /EHs path.
    set_source_files_properties(src/crash_guard.cpp PROPERTIES
        COMPILE_FLAGS "/EHa")
else()
    # MinGW cross-compile
    target_compile_definitions(kcdx PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(kcdx PRIVATE -Wall -Wno-unknown-pragmas)
    target_link_options(kcdx PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Output as <name>.asi instead of <name>.dll
set_target_properties(kcdx PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "kcdx"
)

# -----------------------------------------------------------------------------
# kcdx-watchdog.exe — external crash-bundle helper
# -----------------------------------------------------------------------------
# Spawned by kcdx.asi at startup. Waits on the game's process handle and,
# on non-zero exit, zips kcdx logs + plugin logs + game crash artifacts
# into kcdx-engine/logs/crash/crash_<ts>.zip. See src/watchdog/main.cpp.

add_executable(kcdx-watchdog
    src/watchdog/main.cpp
    vendor/miniz/miniz.c
)
target_include_directories(kcdx-watchdog PRIVATE vendor/miniz)
set_target_properties(kcdx-watchdog PROPERTIES
    OUTPUT_NAME "kcdx-watchdog"
)
if(MSVC)
    target_compile_definitions(kcdx-watchdog PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
        UNICODE _UNICODE)
    target_compile_options(kcdx-watchdog PRIVATE /W3 /MT)
    set_property(TARGET kcdx-watchdog
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_libraries(kcdx-watchdog PRIVATE shell32)
    # /SUBSYSTEM:WINDOWS means no console window appears for the watchdog;
    # /ENTRY:wWinMainCRTStartup picks up our wmain entry-point.
    set_target_properties(kcdx-watchdog PROPERTIES
        WIN32_EXECUTABLE TRUE)
endif()
