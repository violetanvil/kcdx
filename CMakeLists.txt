cmake_minimum_required(VERSION 3.16)
project(kcdx C CXX)

# C++23: safetyhook (the vendored detour backend, hook-backend-marriage) requires
# it (std::expected in its public headers), and its headers are included by kcdx
# engine code, so the whole project compiles as C++23. MSVC maps this to
# /std:c++latest, which carries std::expected on the toolset in use.
set(CMAKE_CXX_STANDARD 23)
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
# SQLite 3 (vendored amalgamation, public domain)
#
# Static lib linked into the engine for the refdb module. Compile-options
# match the recommended embedded read-mostly set:
#   - SQLITE_OMIT_LOAD_EXTENSION: we never load extensions (also a safety win)
#   - SQLITE_THREADSAFE=2: multi-thread mode — each thread its own connection,
#     matching our single-connection-on-the-worker-thread use
#   - SQLITE_DQS=0: disable the double-quoted-string-literal misfeature
#   - SQLITE_DEFAULT_MEMSTATUS=0: skip per-alloc memory accounting (perf)
#   - SQLITE_OMIT_DEPRECATED: drop deprecated interfaces
# MSVC_RUNTIME_LIBRARY is forced to MultiThreaded below (same as minhook/lua)
# so the static runtime matches the rest of the link.
# -----------------------------------------------------------------------------
add_library(sqlite3 STATIC vendor/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC vendor/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=2
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
)

# -----------------------------------------------------------------------------
# BLAKE3 (vendored portable-path source, tri-licensed Apache-2.0 / Apache-2.0
# with LLVM exception / CC0)
#
# Static lib linked into the engine for the survival check (content_hash
# comparison against the on-disk game binary). The portable C fallback is the
# only TU set vendored — the SIMD/.asm translation units are deliberately
# omitted, so all four SIMD code paths are disabled at compile time and
# blake3_dispatch.c routes to blake3_portable. This keeps the build
# toolchain-agnostic (no nasm/.asm assembly step) at the cost of hashing speed;
# the survival check hashes a small per-entity span at apply time, so the
# portable path is fast enough.
# MSVC_RUNTIME_LIBRARY is forced to MultiThreaded below (same as the other
# vendored static libs) so the static runtime matches the rest of the link.
# -----------------------------------------------------------------------------
add_library(blake3 STATIC
    vendor/blake3/blake3.c
    vendor/blake3/blake3_dispatch.c
    vendor/blake3/blake3_portable.c
)
target_include_directories(blake3 PUBLIC vendor/blake3)
target_compile_definitions(blake3 PRIVATE
    BLAKE3_NO_SSE2
    BLAKE3_NO_SSE41
    BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512
)

# -----------------------------------------------------------------------------
# asmjit (vendored flat-copy, Zlib license)
#
# Used by the typed-marshaling trampolines for Lua-callback hooks and
# mid-function hooks. Plugin authors don't see asmjit directly —
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
# Zydis 4.1.0 (vendored amalgamation, MIT license)
#
# The runtime instruction decoder safetyhook requires: at hook-install time it
# decodes the target's live prologue bytes in-process to find a safe instruction
# boundary, size the trampoline, and fix up RIP-relative instructions that move
# into it. kcdx never calls Zydis directly — safetyhook consumes it internally.
# The single-file amalgamation (Zydis.c + Zydis.h) is functionally identical to
# the full source; it is the maintainer's recommended consumption path for a
# consumer that only needs it to link. The MIT notice is carried inline at the
# top of both files.
# -----------------------------------------------------------------------------
add_library(zydis STATIC vendor/zydis/Zydis.c)
target_include_directories(zydis PUBLIC vendor/zydis)
# Static build: make the export macros no-ops so the declarations (Zydis.h) and
# definitions (Zydis.c) agree on linkage. The amalgamation's own note: "For
# static builds, our CMakeList will define ZYDIS_STATIC_BUILD." PUBLIC so
# safetyhook (which includes Zydis.h) compiles against the same decoration.
target_compile_definitions(zydis PUBLIC ZYDIS_STATIC_BUILD ZYAN_STATIC_BUILD)

# -----------------------------------------------------------------------------
# safetyhook 0.6.9 (vendored full source, Boost Software License 1.0)
#
# The thread-safe detour backend kcdx is marrying with MinHook (the
# hook-backend-marriage build): it suspends other threads + fixes their
# instruction pointer while patching, falls back to a far-target absolute jump
# (E9->FF), and provides a vetted mid-hook primitive (MidHook + Context64).
# BSL-1.0 requires the notice only in source copies, not the shipped binary.
# Links Zydis (its required decoder). The Windows mid-hook asm stub
# (mid_hook.x86_64-windows.asm) is NASM syntax (`bits 64`, `[rel x]`), so it
# assembles with NASM, not MASM — build.ps1 puts nasm.exe on PATH for the
# configure step so the VS generator finds it.
# -----------------------------------------------------------------------------
enable_language(ASM_NASM)
add_library(safetyhook STATIC
    vendor/safetyhook/src/allocator.cpp
    vendor/safetyhook/src/inline_hook.cpp
    vendor/safetyhook/src/mid_hook.cpp
    vendor/safetyhook/src/os.windows.cpp
    vendor/safetyhook/src/utility.cpp
    vendor/safetyhook/src/vmt_hook.cpp
    vendor/safetyhook/src/easy.cpp
    vendor/safetyhook/src/mid_hook.x86_64-windows.asm
)
target_include_directories(safetyhook PUBLIC vendor/safetyhook/include)
target_link_libraries(safetyhook PUBLIC zydis)

# -----------------------------------------------------------------------------
# miniz 11.0.2 (vendored single-file, public domain / Unlicense)
#
# Standard DEFLATE/inflate + PKZIP reader. Compiled ONCE into a shared static
# lib linked by BOTH the kcdx engine (its own PKZIP/DEFLATE pak reader — every
# byte on kcdx's CRT, file-system takeover) and kcdx-watchdog (crash-bundle
# zip). MSVC_RUNTIME_LIBRARY is forced to MultiThreaded below (same /MT as the
# other vendored static libs) so the static runtime matches every link.
# -----------------------------------------------------------------------------
add_library(miniz STATIC vendor/miniz/miniz.c)
target_include_directories(miniz PUBLIC vendor/miniz)

# -----------------------------------------------------------------------------
# rom-borrowed (vendored RoM source files for dynamic_hook bindings)
# -----------------------------------------------------------------------------
# add_library(rom_borrowed STATIC
#     vendor/rom-borrowed/memory.cpp
#     vendor/rom-borrowed/runtime_func_t.cpp
#     vendor/rom-borrowed/asmjit_helper.cpp
#     vendor/rom-borrowed/type_info_t.cpp
# )
# target_include_directories(rom_borrowed PUBLIC vendor/rom-borrowed)
# target_link_libraries(rom_borrowed PUBLIC asmjit minhook lua)
# Uncomment when the dedicated rom_borrowed library is split out.

# -----------------------------------------------------------------------------
# kcdx.asi — the engine itself
# -----------------------------------------------------------------------------
# The locator pipeline (byte-rewrite engine, kcdx.toml filename and
# kcdx:: namespace), plus plugin_loader, messaging, trampoline,
# hook_engine, scripting, serialization, etc.
add_library(kcdx SHARED
    src/dllmain.cpp
    src/init_phase.cpp
    src/log.cpp
    src/crash_guard.cpp
    src/watchdog_spawn.cpp
    src/paths.cpp
    src/pe_helpers.cpp
    src/patch_engine.cpp
    src/config.cpp
    src/hooks.cpp
    src/lua_shim.cpp
    src/lua_shim_stubs.cpp
    src/lua_shim_selftest.cpp
    src/lua_vm_build.cpp
    src/cap81_vm_adopt_selftest.cpp
    src/safetyhook_link_selftest.cpp
    src/save_load_hooks.cpp
    src/serialization.cpp
    src/address_library.cpp
    src/console.cpp
    src/cvar.cpp
    src/console_commands_scan.cpp
    src/console_commands_find.cpp
    src/console_commands_dev_inspect.cpp
    src/lua_bind.cpp
    src/plugin_loader.cpp
    src/plugin_pdb.cpp
    src/interfaces.cpp
    src/messaging.cpp
    src/task.cpp
    src/trampoline.cpp
    src/trampoline_selftest.cpp
    src/hook_engine.cpp
    src/modification_inventory.cpp
    src/symbols.cpp
    src/trampoline_engine.cpp
    src/conflict_engine.cpp
    src/load_order.cpp
    src/load_order_edge_selftest.cpp
    src/zone_gate.cpp
    src/ldr_notify.cpp
    src/minhook_backend.cpp
    src/safetyhook_backend.cpp
    src/kcdx_trampoline_registry.cpp
    src/foreign_hook_detect.cpp
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
    src/declared_targets.cpp
    src/target_manifest.cpp
    src/test.cpp
    src/lua_bind_dev.cpp
    src/lua_bind_test.cpp
    src/lua_bind_bytes.cpp
    src/lua_bind_code.cpp
    src/lua_bind_alias.cpp
    src/lua_bind_declare.cpp
    src/lua_bind_scan.cpp
    src/lua_bind_find.cpp
    src/lua_bind_locator.cpp
    src/lua_bind_op.cpp
    src/lua_bind_functions.cpp
    src/lua_bind_hook.cpp
    src/lua_bind_statement.cpp
    src/lua_bind_on.cpp
    src/lua_bind_publish.cpp
    src/lua_bind_command.cpp
    src/lua_bind_cvar.cpp
    src/lua_bind_cosave.cpp
    src/lua_bind_log.cpp
    src/lua_bind_addr.cpp
    src/lua_bind_assets.cpp
    src/lua_bind_behavior.cpp
    src/lua_bind_plugin.cpp
    src/behavior_registry.cpp
    src/behavior_catalog_loader.cpp
    src/lua_registry.cpp
    src/lua_cosave_serial.cpp
    src/lua_lifecycle.cpp
    src/lua_plugin_loader.cpp
    src/lua_require_searcher.cpp
    src/hook_signature.cpp
    src/hook_payload.cpp
    src/hook_chain.cpp
    src/safetyhook_midhook.cpp
    src/hook_interface.cpp
    src/bytes_interface.cpp
    src/declare_interface.cpp
    src/functions_interface.cpp
    src/dll_interface.cpp
    src/statement_interface.cpp
    src/behavior_interface.cpp
    src/asset_interface.cpp
    src/dynamic_call_jit.cpp
    src/early_hook.cpp
    src/asset_overlay.cpp
    src/asset_namespace.cpp
    src/asset_sidecar.cpp
    src/mod_absorb/record_synth.cpp
    src/mod_absorb/record_synth_selftest.cpp
    src/mod_absorb/mod_manifest.cpp
    src/mod_absorb/mod_manifest_selftest.cpp
    src/mod_absorb/pak_mod_registry.cpp
    src/mod_absorb/pak_mod_registry_selftest.cpp
    src/mod_absorb/enabled_list_builder.cpp
    src/mod_absorb/select_detour.cpp
    src/mod_absorb/ctor_bracket.cpp
    src/mod_absorb/record_validate.cpp
    src/mod_absorb/enabled_list_builder_selftest.cpp
    src/mod_absorb/record_validate_selftest.cpp
    src/mod_absorb/order_persist.cpp
    src/mod_absorb/order_persist_selftest.cpp
    src/mod_absorb/mod_absorb_e2e_selftest.cpp
    src/fs_takeover/vtable_table.cpp
    src/fs_takeover/vtable_swap.cpp
    src/fs_takeover/seating_hook.cpp
    src/fs_takeover/pak_reader.cpp
    src/version_compat.cpp
    src/refdb.cpp
    src/blake3.cpp
    src/blake3_selftest.cpp
    src/survival.cpp
    src/survival_verify.cpp
    src/survival_report.cpp
    src/version_check_cache.cpp
    src/survival_pass.cpp
    src/version_check_selftest.cpp
    src/survival_dispatch_selftest.cpp
    src/survival_agreement_selftest.cpp
    src/ki0001_node_classifier_selftest.cpp
    src/statement_resolve_selftest.cpp
    src/find_discovery_selftest.cpp
    src/early_hook_selftest.cpp
    src/foreign_hook_detect_selftest.cpp
    src/foreign_hook_chain_selftest.cpp
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
                             # mid-function hook (kcdx.hook mode=mid) stack_restore_offset calculation.
    vendor/tomlplusplus
    vendor/asmjit            # <asmjit/asmjit.h> for rom_borrowed/
    vendor/blake3            # blake3.h for the survival-check content hash
)

# version.lib is needed for GetFileVersionInfo* calls in plugin_loader.cpp
# asmjit is linked even when no hook uses lua_callback yet — the
# typed-marshaling code references it; the linker dead-strips unused
# asmjit code per-symbol.
target_link_libraries(kcdx PRIVATE lua minhook asmjit sqlite3 blake3 safetyhook miniz version)

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
    set_property(TARGET sqlite3 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET blake3  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET zydis      PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET safetyhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET miniz      PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

    # Emit a PDB beside build/Release/kcdx.dll so a kcdx crash frame
    # symbolicates offline (kcdx+offset crash dumps would otherwise have
    # no symbols).
    # /Zi gives full debug info at compile; /DEBUG links the PDB; /OPT:REF +
    # /OPT:ICF undo the /DEBUG default of disabling those optimizations, so
    # the shipped binary is NOT bloated by enabling debug info. The PDB is
    # kept LOCALLY beside the build and is NEVER shipped (package-release.ps1
    # maps explicit named artifacts only — see that script; .pdb is excluded
    # by omission).
    target_compile_options(kcdx PRIVATE
        $<$<CONFIG:Release>:/Zi>)
    target_link_options(kcdx PRIVATE
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)

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

# Output as kcdx.dll (v0.2 drops the ASI loader dependency in favor of
# kcdx.exe doing CreateRemoteThread injection).
set_target_properties(kcdx PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    OUTPUT_NAME "kcdx"
)

# -----------------------------------------------------------------------------
# kcdx.exe — launcher (v0.2)
# -----------------------------------------------------------------------------
# Standalone Win32 exe the user runs instead of KingdomCome.exe directly.
# Responsibilities:
#   1. Resolve paths: kcdx.exe sits at <game-bin>/; kcdx.dll lives at
#      <game-bin>/engine/kcdx.dll; KingdomCome.exe is the sibling vanilla.
#   2. CreateProcessW(KingdomCome.exe, ..., CREATE_SUSPENDED).
#   3. CreateRemoteThread(LoadLibraryW, "<path>/engine/kcdx.dll") to inject.
#   4. ResumeThread(hMainThread) to let the game run.
# No UI, no dependencies beyond Win32. Strips to <100 KB.
#
# Output name is "kcdx" with suffix ".exe" → produces kcdx.exe. The .exe
# and .dll share the "kcdx" stem; Windows distinguishes by extension.
add_executable(kcdx-launcher
    src/loader/main.cpp
)
set_target_properties(kcdx-launcher PROPERTIES
    PREFIX ""
    SUFFIX ".exe"
    OUTPUT_NAME "kcdx"
    WIN32_EXECUTABLE TRUE
)
if(MSVC)
    target_compile_definitions(kcdx-launcher PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
        UNICODE _UNICODE)
    target_compile_options(kcdx-launcher PRIVATE /W3 /MT)
    set_property(TARGET kcdx-launcher
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# -----------------------------------------------------------------------------
# kcdx-watchdog.exe — external crash-bundle helper
# -----------------------------------------------------------------------------
# Spawned by kcdx.asi at startup. Waits on the game's process handle and,
# on non-zero exit, zips kcdx logs + plugin logs + game crash artifacts
# into kcdx-engine/logs/crash/crash_<ts>.zip. See src/watchdog/main.cpp.

add_executable(kcdx-watchdog
    src/watchdog/main.cpp
)
# miniz is compiled once into the shared `miniz` static lib (above) and linked
# here AND into the kcdx engine — no per-target re-compile of miniz.c. The lib's
# PUBLIC include dir puts <miniz.h> on this target's include path.
target_link_libraries(kcdx-watchdog PRIVATE miniz)
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
