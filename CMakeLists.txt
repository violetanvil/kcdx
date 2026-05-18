cmake_minimum_required(VERSION 3.16)
project(kcdx C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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
# asmjit (Phase 5+: vendored as a submodule, typed Lua-callback marshaling)
# -----------------------------------------------------------------------------
# add_subdirectory(vendor/asmjit)
# Uncomment when Phase 5 lands.

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
    src/pe_helpers.cpp
    src/patch_engine.cpp
    src/config.cpp
    src/hooks.cpp
    src/lua_bind.cpp
)

target_include_directories(kcdx PRIVATE
    src
    include
    vendor/lua
    vendor/minhook/include
    vendor/tomlplusplus
)

target_link_libraries(kcdx PRIVATE lua minhook)

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
