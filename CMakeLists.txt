cmake_minimum_required(VERSION 3.20)
project(MegaManXSNESRecomp C CXX)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SNESRECOMP_BUILD_VERSION "dev" CACHE STRING "Release stamp for crash reports")
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH "snesrecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL "Shared mod package runtime" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)

# Embed the same defaults that are staged beside the executable. Never rewrite
# a player's existing configuration when rebuilding or launching.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config.ini)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config.ini" _config)
string(REPLACE "\\" "\\\\" _config "${_config}")
string(REPLACE "\"" "\\\"" _config "${_config}")
string(REPLACE "\r" "" _config "${_config}")
string(REPLACE "\n" "\\n\"\n\"" _config "${_config}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mmx_default_config.h"
    "static const char kMmxDefaultConfig[] =\n\"${_config}\";\n")

# One source list for Windows, Linux and macOS, with region-specific generated
# banks and funcs.h. Missing optional JP output does not block a USA build.
function(add_mmx_variant TARGET GEN_DIR CFG_DIR)
    if(NOT SNESRECOMP_SETUP_HOST AND NOT EXISTS "${GEN_DIR}/dispatch_v2.c")
        if("${TARGET}" STREQUAL "RockmanXSNESRecomp")
            message(STATUS "Rockman X: generate variants/jp/gen to enable this target")
            return()
        endif()
    endif()
    add_executable(${TARGET} ${SNESRECOMP_RUNNER_SOURCES}
        src/main.c src/mmx_display.c src/mmx_wide_preview.c src/mmx_wide_policy.c
        src/mmx_00.c src/gen_stubs.c src/mmx_cpu_infra.c src/mmx_rtl.c
        src/mmx_spc_player.c src/mods/mmx_widescreen_plugin.c src/mods/mmx_msu1_plugin.c)
    target_include_directories(${TARGET} PRIVATE
        ${CFG_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR} ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(${TARGET} PRIVATE
        SMW_DISABLE_LM=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}> SNESRECOMP_REVERSE_DEBUG=0
        ${ARGN})
    if(NOT MSVC)
        target_compile_options(${TARGET} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    endif()
    target_link_libraries(${TARGET} PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
    snesrecomp_target_sdl(${TARGET})
    snesrecomp_target_fiber_compat(${TARGET})
    snesrecomp_target_desktop_host(${TARGET} TIER2)
    snesrecomp_target_generated_code(${TARGET} ${GEN_DIR})
    if(SNESRECOMP_SETUP_HOST)
        target_sources(${TARGET} PRIVATE src/setup_host.c)
    endif()
    snesrecomp_target_config_seed(${TARGET})
    snesrecomp_target_stage_dir(${TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders" assets/shaders)
    if("${TARGET}" STREQUAL "RockmanXSNESRecomp")
        # JP remains authentic 4:3, with no mod provider.
        snesrecomp_target_mod_catalog(${TARGET} NONE)
    else()
        snesrecomp_target_mod_catalog(${TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
        snesrecomp_target_rom_cache(${TARGET})
        if(NOT SNESRECOMP_SETUP_HOST)
            add_custom_target(${TARGET}_widescreen_overrides
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_overrides.py
                        --gen-dir ${GEN_DIR}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Applying Mega Man X widescreen generated-code overrides" VERBATIM)
            add_dependencies(${TARGET} ${TARGET}_widescreen_overrides)
        endif()
    endif()
    recomp_target_launcher_ui(${TARGET} CONSOLE snes
        BOXART ${CMAKE_CURRENT_SOURCE_DIR}/recomp/launcher/boxart.tga)
endfunction()
set_property(SOURCE src/main.c APPEND PROPERTY COMPILE_DEFINITIONS
    SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")
add_mmx_variant(MegaManXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/src/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp")
add_mmx_variant(RockmanXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/config"
    MMX_VARIANT_JP=1 SNESRECOMP_LLE_BOUNCE_DEFAULT=0)

include(CTest)
if(BUILD_TESTING)
    add_executable(mmx_display_test tests/mmx_display_test.c src/mmx_display.c)
    target_include_directories(mmx_display_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME mmx_display_geometry COMMAND mmx_display_test)
    add_executable(mmx_wide_policy_test tests/mmx_wide_policy_test.c src/mmx_wide_policy.c)
    target_include_directories(mmx_wide_policy_test PRIVATE src)
    add_test(NAME mmx_wide_policy COMMAND mmx_wide_policy_test)
endif()
include(${SNESRECOMP_ROOT}/runner/cmake/mmx_state_tests.cmake)
snesrecomp_target_mmx_state_tests(MegaManXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")
