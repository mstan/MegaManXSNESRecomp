cmake_minimum_required(VERSION 3.16)
project(MegaManXSNESRecomp C CXX)

include(CTest)

if(APPLE)
    enable_language(OBJCXX)
endif()

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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(_MMX_SNESRECOMP_SUBMODULE "${CMAKE_SOURCE_DIR}/snesrecomp")
set(SNESRECOMP_ROOT "${_MMX_SNESRECOMP_SUBMODULE}" CACHE PATH
    "Path to the snesrecomp dependency checkout")
option(MMX_ALLOW_UNPINNED_SNESRECOMP
       "Allow building against a different or dirty snesrecomp checkout" OFF)

if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
      "snesrecomp is not initialized at ${SNESRECOMP_ROOT}. "
      "Run `bash tools/bootstrap.sh` from the repository root, then configure again.")
endif()

if(NOT MMX_ALLOW_UNPINNED_SNESRECOMP)
    get_filename_component(_MMX_SNESRECOMP_ROOT_REAL "${SNESRECOMP_ROOT}" REALPATH)
    get_filename_component(_MMX_SNESRECOMP_SUBMODULE_REAL
                           "${_MMX_SNESRECOMP_SUBMODULE}" REALPATH)
    if(NOT _MMX_SNESRECOMP_ROOT_REAL STREQUAL _MMX_SNESRECOMP_SUBMODULE_REAL)
        message(FATAL_ERROR
          "SNESRECOMP_ROOT points outside the pinned submodule. "
          "Use -DMMX_ALLOW_UNPINNED_SNESRECOMP=ON when intentionally developing "
          "against another checkout.")
    endif()

    find_package(Git QUIET)
    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR
          "Git is required to verify the pinned snesrecomp submodule. "
          "Install Git or explicitly configure with "
          "-DMMX_ALLOW_UNPINNED_SNESRECOMP=ON for a source archive.")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
                ls-files --stage -- snesrecomp
        OUTPUT_VARIABLE _MMX_SNESRECOMP_GITLINK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _MMX_SNESRECOMP_GITLINK_RC
        ERROR_QUIET)
    if(NOT _MMX_SNESRECOMP_GITLINK_RC EQUAL 0 OR
       NOT _MMX_SNESRECOMP_GITLINK MATCHES
           "^160000 +([0-9a-fA-F]+) +0")
        message(FATAL_ERROR
          "Could not read the pinned snesrecomp gitlink. "
          "Use a Git clone of this repository or explicitly configure with "
          "-DMMX_ALLOW_UNPINNED_SNESRECOMP=ON for a source archive.")
    endif()
    set(_MMX_PIN_SHA "${CMAKE_MATCH_1}")

    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SNESRECOMP_ROOT}"
                            rev-parse HEAD
                    OUTPUT_VARIABLE _MMX_RUNNER_SHA
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _MMX_RUNNER_SHA_RC)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SNESRECOMP_ROOT}"
                status --porcelain=v1 --untracked-files=no --ignore-submodules=none
        OUTPUT_VARIABLE _MMX_RUNNER_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _MMX_RUNNER_STATUS_RC)
    if(NOT _MMX_RUNNER_SHA_RC EQUAL 0 OR
       NOT _MMX_RUNNER_STATUS_RC EQUAL 0 OR
       NOT _MMX_RUNNER_SHA STREQUAL _MMX_PIN_SHA OR
       NOT _MMX_RUNNER_STATUS STREQUAL "")
        message(FATAL_ERROR
          "snesrecomp is not the clean pinned revision ${_MMX_PIN_SHA}. "
          "Run `git submodule update --init --recursive` to restore it, or "
          "explicitly configure with -DMMX_ALLOW_UNPINNED_SNESRECOMP=ON while "
          "developing framework changes.")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(mmx_display_test tests/mmx_display_test.c src/mmx_display.c)
    target_include_directories(mmx_display_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME mmx_display_geometry COMMAND mmx_display_test)
endif()

include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# The runner launcher supplies the game-agnostic C pieces. The fork owns the
# ImGui copy used by both the launcher and native platform UI; using the
# launcher's older bundled ImGui would create duplicate symbols and an API
# mismatch with the Metal backend.
function(mmx_target_launcher_ng TGT)
    cmake_parse_arguments(LNG "" "BOXART" "" ${ARGN})
    set(LNG_ROOT ${SNESRECOMP_ROOT}/runner/src/launcher_ng)
    set(LNG_IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
    target_sources(${TGT} PRIVATE
        ${LNG_ROOT}/launcher_model.c
        ${LNG_ROOT}/launcher_platform_sdl2.c
        ${LNG_ROOT}/launcher_gl.c
        ${LNG_ROOT}/launcher_input.c
        ${LNG_ROOT}/launcher_files.c
        ${LNG_ROOT}/launcher_debug.c
        ${LNG_ROOT}/launcher_binds.c
        ${LNG_ROOT}/launcher_ng_capi.c
        ${LNG_ROOT}/third_party/tinyfiledialogs.c
        ${LNG_ROOT}/backends/imgui/launcher_imgui.cpp
        ${LNG_IMGUI}/imgui.cpp
        ${LNG_IMGUI}/imgui_draw.cpp
        ${LNG_IMGUI}/imgui_tables.cpp
        ${LNG_IMGUI}/imgui_widgets.cpp
        ${LNG_IMGUI}/backends/imgui_impl_sdl2.cpp
        ${LNG_IMGUI}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(${TGT} PRIVATE
        ${LNG_ROOT}/../launcher ${LNG_ROOT}
        ${LNG_ROOT}/third_party ${LNG_IMGUI}
        ${LNG_IMGUI}/backends)
    target_compile_definitions(${TGT} PRIVATE SNES_LAUNCHER SDL_MAIN_HANDLED)
    set_target_properties(${TGT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_custom_command(TARGET ${TGT} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TGT}>/assets/fonts
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TGT}>/assets/img
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LNG_ROOT}/../launcher/assets/fonts/LatoLatin-Regular.ttf
            ${LNG_ROOT}/../launcher/assets/fonts/LatoLatin-Bold.ttf
            $<TARGET_FILE_DIR:${TGT}>/assets/fonts/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LNG_ROOT}/../launcher/assets/img/brand_mark.tga
            ${LNG_ROOT}/../launcher/assets/img/snes_pad.tga
            $<TARGET_FILE_DIR:${TGT}>/assets/img/
        VERBATIM)
    if(LNG_BOXART AND EXISTS ${LNG_BOXART})
        add_custom_command(TARGET ${TGT} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LNG_BOXART}
                $<TARGET_FILE_DIR:${TGT}>/assets/img/boxart.tga VERBATIM)
    endif()
endfunction()

find_package(SDL2 REQUIRED)
if(NOT APPLE)
    find_package(OpenGL REQUIRED)
endif()

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
        src/mmx_display.c
        $<$<NOT:$<PLATFORM_ID:Darwin>>:src/glsl_shader.c>
        src/main.c
        src/mmx_wide_preview.c
        $<$<PLATFORM_ID:Linux>:src/linux_ui.cpp>
        src/post_mortem.c
        $<$<NOT:$<PLATFORM_ID:Darwin>>:src/opengl.c>
        src/mmx_00.c
        src/gen_stubs.c
        src/mmx_cpu_infra.c
        src/mmx_rtl.c
        src/mmx_spc_player.c
        src/fiber_compat.c    # Win32 Fiber API shim (ucontext) for non-Windows

        $<$<PLATFORM_ID:Darwin>:src/macos_backend.m>

        # Native macOS UI and optional NTSC CRT display filter.
        $<$<PLATFORM_ID:Darwin>:src/macos_ui.mm>
        # launcher_ng supplies the single shared ImGui core, SDL2 backend, and
        # OpenGL3 backend. Keep only the game UI's platform renderer here.
        $<$<PLATFORM_ID:Darwin>:third_party/imgui/backends/imgui_impl_metal.mm>
        $<$<PLATFORM_ID:Linux>:${SNESRECOMP_ROOT}/runner/src/launcher_ng/third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp>
        $<$<PLATFORM_ID:Darwin>:third_party/ntsc-crt/crt_core.c>
        $<$<PLATFORM_ID:Darwin>:third_party/ntsc-crt/crt_snes.c>

        # OpenGL function loader
        $<$<NOT:$<PLATFORM_ID:Darwin>>:third_party/gl_core/gl_core_3_1.c>
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

    if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_include_directories(${TARGET} PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/imgui
            ${CMAKE_SOURCE_DIR}/third_party/imgui/backends)
    endif()
    if(APPLE)
        target_include_directories(${TARGET} PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/ntsc-crt)
    endif()

    target_compile_definitions(${TARGET} PRIVATE
        SMW_DISABLE_LM=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
        SNESRECOMP_TRACE=${_MMX_TRACE}    # TCP debug server: 0 in prod, 1 in debug
        SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
        ${ARGN}
    )

    # host_report.c uses dl_phdr_info for Linux module diagnostics; glibc hides
    # that declaration unless this feature-test macro is set before headers.
    if(UNIX AND NOT APPLE)
        target_compile_definitions(${TARGET} PRIVATE _GNU_SOURCE=1)
    endif()

    if(APPLE)
        target_compile_definitions(${TARGET} PRIVATE CRT_SYSTEM=3)
    endif()

    # The recompiled C is machine-generated; quiet its warnings and tolerate the
    # K&R-style implicit declarations clang rejects by default.
    if(NOT MSVC)
        target_compile_options(${TARGET} PRIVATE
            -w -Wno-implicit-function-declaration)
    endif()

    target_link_libraries(${TARGET} PRIVATE
        m                                 # libm: runtime math
        ${SNESRECOMP_RUNNER_LIBRARIES}    # ws2_32 when trace/cosim builds add the TCP servers
    )
    if(APPLE)
        target_link_libraries(${TARGET} PRIVATE
            "-framework Metal"
            "-framework QuartzCore"
            "-framework GameController"
            "-framework Foundation"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework OpenGL")
        target_compile_definitions(${TARGET} PRIVATE MMX_MACOS_NATIVE=1)
    else()
        target_link_libraries(${TARGET} PRIVATE OpenGL::GL)
    endif()
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

    # Dear ImGui pre-boot launcher (GUI). One call wires sources/includes/
    # SNES_LAUNCHER/CXX/asset-staging; see launcher_ng/launcher_ng.cmake.
    mmx_target_launcher_ng(${TARGET}
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
    if(APPLE)
        set(_MMX_ICON ${CMAKE_SOURCE_DIR}/assets/MegaManX.icns)
        target_sources(${TARGET} PRIVATE ${_MMX_ICON})
        set_source_files_properties(${_MMX_ICON} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(${TARGET} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.mstan.megamanxrecomp"
            MACOSX_BUNDLE_BUNDLE_NAME "Mega Man X Recomp"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
             MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0")
    endif()
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
