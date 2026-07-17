cmake_minimum_required(VERSION 3.16)
project(MegaManXSNESRecomp C)

# Cross-platform (macOS/Linux) build for MegaManXSNESRecomp.
#
# One repo, two games (the RubySapphireRecomp pattern):
#   MegaManXSNESRecomp   Mega Man X (USA v1.1)     gen=src/gen           LLE scheduler
#   RockmanXSNESRecomp   Rockman X (Japan v1.1)    gen=variants/jp/gen   LLE scheduler
#
# Both targets share the same runner + game glue (src/*.c); only the
# recompiled banks and game-identity compile-defs (MMX_VARIANT_JP) differ.
# Both use the faithful LLE task scheduler by default; the legacy C-host HLE
# is an explicit shared execution-mode override.
#
# The canonical Windows build is the MSVC solution (mmx.sln, USA only for now).
# This CMake build mirrors the same source list (see snesrecomp/runner/
# runner.cmake) so the games build natively on macOS/Linux/mingw with
# clang/gcc + SDL2 + OpenGL.
#
# Prerequisites: a snesrecomp checkout at ./snesrecomp; the generated C for the
# variant(s) you build (run `bash tools/regen.sh [usa|jp]` with the verified
# ROM staged); SDL2 (e.g. `brew install sdl2`).

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui submodule/junction) so every recomp game drives
# ONE launcher repo. The former in-tree launcher_ng has been retired from the
# engine in favour of this shared, console-agnostic launcher.
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# prod-vs-debug via the single SNESRECOMP_ENABLE_TRACE switch (runner.cmake,
# default OFF). Prod: no debug_server.c / TCP server / rings. Debug:
# -DSNESRECOMP_ENABLE_TRACE=ON. tools/build-linux.sh --config {prod,debug} sets it.
if(SNESRECOMP_ENABLE_TRACE)
    set(_MMX_TRACE 1)
else()
    set(_MMX_TRACE 0)
endif()

# add_mmx_variant(<target> <gen_dir> <cfg_include_dir> [extra compile defs...])
#
# gen_dir         directory of recompiler-emitted bank C (globbed).
# cfg_include_dir directory holding this variant's generated funcs.h.
# extra defs      per-variant target_compile_definitions (e.g. MMX_VARIANT_JP=1).
function(add_mmx_variant TARGET GEN_DIR CFG_INCLUDE_DIR)
    file(GLOB _MMX_GEN CONFIGURE_DEPENDS ${GEN_DIR}/*.c)
    if(NOT _MMX_GEN)
        message(WARNING
            "${TARGET}: ${GEN_DIR} is empty — skipping this target. "
            "Run `bash tools/regen.sh` for this variant to populate it.")
        return()
    endif()

    add_executable(${TARGET}
        ${SNESRECOMP_RUNNER_SOURCES}
        # game-specific runtime (shared across both variants)
        src/config.c
        src/glsl_shader.c
        src/main.c
        src/post_mortem.c
        src/opengl.c
        src/mmx_00.c
        src/gen_stubs.c
        src/mmx_cpu_infra.c
        src/mmx_rtl.c
        src/mmx_spc_player.c
        src/fiber_compat.c    # Win32 Fiber API shim (ucontext) for non-Windows

        # OpenGL function loader
        third_party/gl_core/gl_core_3_1.c
        # recompiled banks (generated, per-variant)
        ${_MMX_GEN}
    )

    # NOTE: CFG_INCLUDE_DIR must precede any other dir that also holds a
    # funcs.h so the generated banks pick up THIS variant's declarations.
    target_include_directories(${TARGET} PRIVATE
        ${CFG_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
    )

    target_compile_definitions(${TARGET} PRIVATE
        SMW_DISABLE_LM=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
        SNESRECOMP_TRACE=${_MMX_TRACE}    # TCP debug server: 0 in prod, 1 in debug
        SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
        ${ARGN}
    )

    # The recompiled C is machine-generated; quiet its warnings and tolerate the
    # K&R-style implicit declarations clang rejects by default.
    if(NOT MSVC)
        target_compile_options(${TARGET} PRIVATE
            -w -Wno-implicit-function-declaration)
    endif()

    target_link_libraries(${TARGET} PRIVATE
        OpenGL::GL
        m                                 # libm: glsl_shader.c uses pow() (implicit on macOS)
        ${SNESRECOMP_RUNNER_LIBRARIES}    # ws2_32 when trace/cosim builds add the TCP servers
    )
    if(WIN32 AND NOT MSVC)
        # mingw/msys2: this SDL2 does not auto-rename main()->SDL_main, so main.c's
        # real main() is the entry point — link SDL2 WITHOUT SDL2main (SDL2main's
        # WinMain would demand an SDL_main that doesn't exist). Plus DbgHelp for
        # post_mortem.c's StackWalk64 backtrace.
        target_link_libraries(${TARGET} PRIVATE
            C:/msys64/mingw64/lib/libSDL2.dll.a
            dbghelp)
    else()
        target_link_libraries(${TARGET} PRIVATE ${SDL2_LIBRARIES})
    endif()

    # Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
    # wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same MMX
    # boxart; recomp-ui's default pad.tga is already the SNES pad).
    recomp_target_launcher_ui(${TARGET}
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
endfunction()

# ── USA: Mega Man X (v1.1) — faithful LLE scheduler by default ──
add_mmx_variant(MegaManXSNESRecomp
    ${CMAKE_SOURCE_DIR}/src/gen
    ${CMAKE_SOURCE_DIR}/recomp)

# ── JP: Rockman X (Japan v1.1) — faithful LLE scheduler, no task-PC table ──
add_mmx_variant(RockmanXSNESRecomp
    ${CMAKE_SOURCE_DIR}/variants/jp/gen
    ${CMAKE_SOURCE_DIR}/variants/jp/config
    MMX_VARIANT_JP=1
    # 2026-07-03 status: after the first tier-2 enrichment round (131 promoted
    # funcs; the old 3-func cfg's bounced-vs-interp split at cp2 was the
    # unenriched gen mis-slicing), JP now PASSES bounced-vs-interpreted co-sim
    # 0/4000 (only the masked $02FF interrupt-residue byte) and A-vs-A with
    # bounce on. Default stays OFF pending deeper-gameplay coverage + owner
    # sign-off; env SNESRECOMP_LLE_BOUNCE=1 opts in.
    SNESRECOMP_LLE_BOUNCE_DEFAULT=0)
