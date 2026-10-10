cmake_minimum_required(VERSION 3.20)
project(MegaManXSNESRecomp VERSION 2.1.8 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_x_weapon_tables.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/data/x_weapon_assets.json"
        "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h"
    DEPENDS tools/generate_x_weapon_tables.py tools/data/x_weapon_assets.json VERBATIM)

set(SNESRECOMP_BUILD_VERSION "${PROJECT_VERSION}-alpha" CACHE STRING "Release stamp for crash reports")
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH "snesrecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL "Shared mod package runtime" FORCE)
# Match Gundam Endless Duel's internet-capable lobby build. Per-target netplay
# keeps the unmodified Japanese edition and small unit tests independent.
set(SNESRECOMP_NET_ICE ON CACHE BOOL "Internet netplay transport" FORCE)
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
        src/main.c src/mmx_startup.c src/mmx_netplay.c src/mmx_display.c src/mmx_renderer.c src/mmx_render_assets.c src/mmx_wide_policy.c
        src/mmx_00.c src/gen_stubs.c src/mmx_cpu_infra.c src/mmx_rtl.c
        src/mmx_spc_player.c src/mods/mmx_diagnostics_plugin.c
        src/mods/mmx_widescreen_plugin.c src/mods/mmx_msu1_plugin.c
        src/mods/mmx_hadouken_plugin.c src/mods/mmx_password_plugin.c src/mmx_password_save.cpp
        src/mods/mmx_zero_plugin.c src/mmx_zero.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_knc_bugfix.c src/mmx_coop.c src/mmx_coop_trace.c src/mmx_coop_view.c
        src/mmx_hitbox_overlay.c src/mmx_source_assets.cpp "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h")
    target_compile_features(${TARGET} PRIVATE cxx_std_17)
    target_include_directories(${TARGET} PRIVATE
        ${CFG_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    snesrecomp_target_shader_presets(${TARGET})
    if("${TARGET}" STREQUAL "RockmanXSNESRecomp")
        # JP remains authentic 4:3, with no mod provider.
        snesrecomp_target_mod_catalog(${TARGET} NONE)
    else()
        snesrecomp_target_mod_catalog(${TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
        snesrecomp_target_rom_cache(${TARGET})
        if(NOT SNESRECOMP_SETUP_HOST)
            # Invalidate generated objects before Ninja plans this build.
            file(GLOB mmx_generated ${GEN_DIR}/*.c)
            set_property(SOURCE ${mmx_generated} APPEND PROPERTY OBJECT_DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_overrides.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_renderer_capture.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_password_hooks.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_zero_hooks.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_coop_hooks.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_weapon_hooks.py)
            add_custom_target(${TARGET}_widescreen_overrides
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_overrides.py
                        --gen-dir ${GEN_DIR}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_renderer_capture.py
                        --gen-dir ${GEN_DIR}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_password_hooks.py
                        --gen-dir ${GEN_DIR}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_zero_hooks.py
                        --gen-dir ${GEN_DIR}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_weapon_hooks.py
                        --gen-dir ${GEN_DIR}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_coop_hooks.py
                        --gen-dir ${GEN_DIR}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Applying Mega Man X generated-code mod hooks" VERBATIM)
            add_dependencies(${TARGET} ${TARGET}_widescreen_overrides)
        endif()
    endif()
    if(WIN32)
        set_target_properties(${TARGET} PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    # Logging policy beside the executable on every platform; never replace
    # a player's edited copy.
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/logging.ini")
        configure_file(logging.ini logging.ini COPYONLY)
    endif()
    recomp_target_launcher_ui(${TARGET} CONSOLE snes
        BOXART ${CMAKE_CURRENT_SOURCE_DIR}/recomp/launcher/boxart.tga)
    if(NOT "${TARGET}" STREQUAL "RockmanXSNESRecomp")
        snesrecomp_enable_recomp_net(${TARGET})
    endif()
endfunction()
set_property(SOURCE src/main.c APPEND PROPERTY COMPILE_DEFINITIONS
    SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")
set(MMX_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/gen" CACHE PATH
    "Generated USA program directory (allows isolated coverage comparisons)")
add_mmx_variant(MegaManXSNESRecomp "${MMX_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp")
add_mmx_variant(RockmanXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/config"
    MMX_VARIANT_JP=1 SNESRECOMP_LLE_BOUNCE_DEFAULT=0)

include(CTest)
if(BUILD_TESTING)
    add_executable(mmx_source_assets_test tests/mmx_source_assets_test.cpp src/mmx_source_assets.cpp
        ${SNESRECOMP_ROOT}/runner/src/sha256.c "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h")
    target_include_directories(mmx_source_assets_test PRIVATE src ${CMAKE_CURRENT_BINARY_DIR} ${SNESRECOMP_ROOT}/runner/src)
    target_compile_features(mmx_source_assets_test PRIVATE cxx_std_17)
    add_executable(mmx_zero_test tests/mmx_zero_test.c src/mmx_zero.c)
    target_include_directories(mmx_zero_test PRIVATE src)
    add_test(NAME mmx_zero_character COMMAND mmx_zero_test)
    add_executable(mmx_password_save_test tests/mmx_password_save_test.cpp src/mmx_password_save.cpp)
    target_include_directories(mmx_password_save_test PRIVATE src)
    target_compile_features(mmx_password_save_test PRIVATE cxx_std_17)
    add_test(NAME mmx_password_save COMMAND mmx_password_save_test)
    add_executable(mmx_display_test tests/mmx_display_test.c src/mmx_display.c)
    target_include_directories(mmx_display_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME mmx_display_geometry COMMAND mmx_display_test)
    add_executable(mmx_wide_policy_test tests/mmx_wide_policy_test.c src/mmx_wide_policy.c)
    target_include_directories(mmx_wide_policy_test PRIVATE src)
    add_test(NAME mmx_wide_policy COMMAND mmx_wide_policy_test)
    add_executable(mmx_renderer_test tests/mmx_renderer_test.c src/mmx_renderer.c src/mmx_render_assets.c src/mmx_display.c src/mmx_wide_policy.c src/mmx_zero.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_coop_view.c)
    target_include_directories(mmx_renderer_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME mmx_custom_renderer COMMAND mmx_renderer_test)
    add_executable(mmx_render_capture tools/mmx_render_capture.c src/mmx_renderer.c src/mmx_render_assets.c src/mmx_display.c src/mmx_wide_policy.c src/mmx_zero.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_coop_view.c)
    target_include_directories(mmx_render_capture PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    foreach(mmx_test mmx_display_test mmx_wide_policy_test mmx_renderer_test mmx_password_save_test mmx_zero_test)
        if(MSVC)
            target_compile_options(${mmx_test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${mmx_test} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    if(NOT WIN32)
        target_link_libraries(mmx_renderer_test PRIVATE m)
        target_link_libraries(mmx_render_capture PRIVATE m)
    endif()
    # End to end through the real executable (#45: Reset must reboot). Needs
    # the ROM, so only where one is staged -- never in CI.
    if(TARGET MegaManXSNESRecomp AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc")
        add_test(NAME mmx_reset_reboots
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/reset_boot_test.py
                --exe $<TARGET_FILE:MegaManXSNESRecomp>
                --rom ${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc
                --config ${CMAKE_CURRENT_SOURCE_DIR}/config.ini)
    endif()
endif()
include(${SNESRECOMP_ROOT}/runner/cmake/mmx_state_tests.cmake)
snesrecomp_target_mmx_state_tests(MegaManXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

if(TARGET mmx_state_tests)
    add_dependencies(mmx_state_tests MegaManXSNESRecomp_widescreen_overrides)
    target_compile_definitions(mmx_state_tests PRIVATE
        MMX_ZERO_COMBAT_REFERENCE_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/zero_x3_combat.csv"
        MMX_ZERO_MOTION_REFERENCE_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/zero_x3_motion.csv")
    get_target_property(mmx_state_sources mmx_state_tests SOURCES)
    list(REMOVE_ITEM mmx_state_sources "${SNESRECOMP_ROOT}/runner/tests/mmx_state_runtime.c")
    set_property(TARGET mmx_state_tests PROPERTY SOURCES
        tests/mmx_adaptive_state_test.c ${mmx_state_sources})
endif()
include(cmake/saber.cmake)
