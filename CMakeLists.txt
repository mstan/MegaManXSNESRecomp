cmake_minimum_required(VERSION 3.20)
project(MegaManXSNESRecomp VERSION 2.3.8 LANGUAGES C CXX)
# Qualified Windows x64 default; retain explicit selections and other-target floors.
set(_MMX_RASTER_IMPL_DEFAULT "LLE")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _MMX_RASTER_IMPL_PROCESSOR)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
   _MMX_RASTER_IMPL_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
    set(_MMX_RASTER_IMPL_DEFAULT "NATIVE")
endif()
set(MMX_RASTER_IMPL "${_MMX_RASTER_IMPL_DEFAULT}" CACHE STRING "Raster production (LLE or NATIVE)")
set_property(CACHE MMX_RASTER_IMPL PROPERTY STRINGS LLE NATIVE)
if(NOT MMX_RASTER_IMPL MATCHES "^(LLE|NATIVE)$")
    message(FATAL_ERROR "MMX_RASTER_IMPL must be LLE or NATIVE")
endif()
message(STATUS "MMX raster production: ${MMX_RASTER_IMPL}")
set(MMX_RENDERER_SOURCE src/mmx_renderer.c)
if(MMX_RASTER_IMPL STREQUAL "NATIVE")
    set(MMX_RENDERER_SOURCE src/mmx_renderer_native.c)
endif()
set_property(SOURCE src/main.c src/mmx_rtl.c tests/mmx_adaptive_state_test.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    MMX_NATIVE_RASTER=$<STREQUAL:${MMX_RASTER_IMPL},NATIVE>)
set(CMAKE_C_STANDARD 11)
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH "snesrecomp checkout")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Git QUIET)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MMX_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MMX_SOURCE_REVISION)
    set(MMX_SOURCE_REVISION "unknown")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=12
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MMX_REPLAY_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=12
    WORKING_DIRECTORY "${SNESRECOMP_ROOT}"
    OUTPUT_VARIABLE MMX_REPLAY_ENGINE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_x_weapon_tables.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/data/x_weapon_assets.json"
        "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h"
    DEPENDS tools/generate_x_weapon_tables.py tools/data/x_weapon_assets.json VERBATIM)

set(SNESRECOMP_BUILD_VERSION "${PROJECT_VERSION}" CACHE STRING "Release stamp for crash reports")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
option(MMX_RASTER_FULL_COPY "Use full-copy raster storage as the correctness/performance reference" OFF)
option(MMX_STAGE_TILE_REUSE "Reuse exact stage-map lookups within one captured draw" ON)
set(_MMX_NATIVE_ROW_PLAN_DEFAULT OFF)
if(_MMX_RASTER_IMPL_DEFAULT STREQUAL "NATIVE" AND MMX_RASTER_IMPL STREQUAL "NATIVE")
    set(_MMX_NATIVE_ROW_PLAN_DEFAULT ON)
endif()
option(MMX_NATIVE_ROW_PLAN "Prepare whole-view native row composition plans" ${_MMX_NATIVE_ROW_PLAN_DEFAULT})
set_property(SOURCE src/mmx_renderer_native.c tests/mmx_native_renderer_test.c
    src/main.c tests/mmx_adaptive_state_test.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    MMX_NATIVE_ROW_PLAN=$<BOOL:${MMX_NATIVE_ROW_PLAN}>)
set_property(SOURCE src/mmx_renderer.c APPEND PROPERTY COMPILE_DEFINITIONS
    MMX_RASTER_FULL_COPY=$<BOOL:${MMX_RASTER_FULL_COPY}>
    MMX_STAGE_TILE_REUSE=$<BOOL:${MMX_STAGE_TILE_REUSE}>)
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL "Shared mod package runtime" FORCE)
# Match Gundam Endless Duel's internet-capable lobby build. Per-target netplay
# keeps the unmodified Japanese edition and small unit tests independent.
set(SNESRECOMP_NET_ICE ON CACHE BOOL "Internet netplay transport" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
string(APPEND MMX_REPLAY_ENGINE "-apu${SNESRECOMP_APU_TIME_IMPL}-crc${SNESRECOMP_CRC32_IMPL}-raster${MMX_RASTER_IMPL}-row${MMX_NATIVE_ROW_PLAN}")
set_property(SOURCE src/mmx_replay.c APPEND PROPERTY COMPILE_DEFINITIONS
    MMX_REPLAY_REVISION="${MMX_REPLAY_REVISION}" MMX_REPLAY_ENGINE="${MMX_REPLAY_ENGINE}")
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
        src/main.c src/mmx_startup.c src/mmx_netplay.c src/mmx_display.c ${MMX_RENDERER_SOURCE} src/mmx_render_assets.c src/mmx_wide_policy.c
        src/mmx_00.c src/gen_stubs.c src/mmx_cpu_infra.c src/mmx_rtl.c
        src/mmx_spc_player.c src/mmx_replay.c src/mods/mmx_diagnostics_plugin.c
        src/mods/mmx_widescreen_plugin.c src/mods/mmx_msu1_plugin.c
        src/mods/mmx_hadouken_plugin.c src/mods/mmx_password_plugin.c src/mods/mmx_boss_rush_plugin.c src/mmx_password_save.cpp
        src/mods/mmx_zero_plugin.c src/mods/mmx_armor_plugin.c src/mmx_zero.c src/mmx_armor.c src/mmx_armor_launcher.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_knc_bugfix.c src/mmx_coop.c src/mmx_coop_trace.c src/mmx_coop_view.c
        src/mmx_hitbox_overlay.c
        src/mmx_boss_rush.c src/mmx_boss_rush_audio.c src/mmx_boss_rush_runtime.c src/mmx_boss_rush_draw.c src/mmx_boss_rush_score.cpp
        src/mmx_source_assets.cpp "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h")
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
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_weapon_hooks.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_boss_rush_hooks.py)
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
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_boss_rush_hooks.py
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
set_property(SOURCE src/main.c src/mmx_replay.c APPEND PROPERTY COMPILE_DEFINITIONS
    SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}"
    MMX_SOURCE_REVISION="${MMX_SOURCE_REVISION}")
set(MMX_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/gen" CACHE PATH
    "Generated USA program directory (allows isolated coverage comparisons)")
add_mmx_variant(MegaManXSNESRecomp "${MMX_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp")
add_mmx_variant(RockmanXSNESRecomp "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/variants/jp/config"
    MMX_VARIANT_JP=1 SNESRECOMP_LLE_BOUNCE_DEFAULT=0)

include(CTest)
if(BUILD_TESTING)
    add_executable(mmx_armor_launcher_test tests/mmx_armor_launcher_test.c src/mmx_armor_launcher.c)
    target_include_directories(mmx_armor_launcher_test PRIVATE src ${RECOMP_UI_ROOT}/src)
    target_compile_definitions(mmx_armor_launcher_test PRIVATE RECOMP_LAUNCHER=1)
    if(MSVC)
        target_compile_options(mmx_armor_launcher_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_armor_launcher_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mmx_armor_launcher COMMAND mmx_armor_launcher_test)
    add_executable(mmx_armor_test tests/mmx_armor_test.c src/mmx_armor.c)
    target_include_directories(mmx_armor_test PRIVATE src)
    if(MSVC)
        target_compile_options(mmx_armor_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_armor_test PRIVATE -UNDEBUG)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/armor-test")
    add_test(NAME mmx_armor COMMAND mmx_armor_test)
    set_tests_properties(mmx_armor PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/armor-test")
    add_subdirectory(${SNESRECOMP_ROOT}/runner/tests/raster_snapshot raster-snapshot-tests)
    add_subdirectory(${SNESRECOMP_ROOT}/runner/tests/diagnostics diagnostic-writer-tests)
    add_executable(mmx_boss_rush_test tests/mmx_boss_rush_test.c src/mmx_boss_rush.c src/mmx_boss_rush_draw.c)
    target_include_directories(mmx_boss_rush_test PRIVATE src)
    add_test(NAME mmx_boss_rush COMMAND mmx_boss_rush_test)
    add_executable(mmx_boss_rush_score_test tests/mmx_boss_rush_score_test.cpp src/mmx_boss_rush_score.cpp)
    target_include_directories(mmx_boss_rush_score_test PRIVATE src)
    target_compile_features(mmx_boss_rush_score_test PRIVATE cxx_std_17)
    add_test(NAME mmx_boss_rush_score COMMAND mmx_boss_rush_score_test)
    add_executable(mmx_msu1_test tests/mmx_msu1_test.c src/mods/mmx_msu1_plugin.c
        ${SNESRECOMP_ROOT}/runner/src/snes/msu1.c)
    target_include_directories(mmx_msu1_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/msu-test")
    add_test(NAME mmx_msu1 COMMAND mmx_msu1_test)
    set_tests_properties(mmx_msu1 PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/msu-test")
    add_executable(mmx_source_assets_test tests/mmx_source_assets_test.cpp src/mmx_source_assets.cpp
        ${SNESRECOMP_ROOT}/runner/src/sha256.c "${CMAKE_CURRENT_BINARY_DIR}/mmx_weapon_sources.h")
    target_include_directories(mmx_source_assets_test PRIVATE src ${CMAKE_CURRENT_BINARY_DIR} ${SNESRECOMP_ROOT}/runner/src)
    target_compile_features(mmx_source_assets_test PRIVATE cxx_std_17)
    add_executable(mmx_zero_test tests/mmx_zero_test.c src/mmx_zero.c src/mmx_armor.c)
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
    add_executable(mmx_renderer_test tests/mmx_renderer_test.c src/mmx_renderer.c src/mmx_render_assets.c src/mmx_display.c src/mmx_wide_policy.c src/mmx_zero.c src/mmx_armor.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_coop_view.c src/mmx_boss_rush.c src/mmx_boss_rush_draw.c)
    target_include_directories(mmx_renderer_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME mmx_custom_renderer COMMAND mmx_renderer_test)
    get_target_property(mmx_renderer_reference_sources mmx_renderer_test SOURCES)
    list(REMOVE_ITEM mmx_renderer_reference_sources src/mmx_renderer.c)
    set(mmx_renderer_native_test_sources ${mmx_renderer_reference_sources})
    list(REMOVE_ITEM mmx_renderer_native_test_sources tests/mmx_renderer_test.c)
    add_executable(mmx_renderer_native_test ${mmx_renderer_native_test_sources}
        tests/mmx_native_renderer_test.c src/mmx_boss_rush_score.cpp)
    target_include_directories(mmx_renderer_native_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    target_compile_features(mmx_renderer_native_test PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(mmx_renderer_native_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_renderer_native_test PRIVATE -UNDEBUG)
    endif()
    if(NOT WIN32)
        target_link_libraries(mmx_renderer_native_test PRIVATE m)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-native")
    add_test(NAME mmx_custom_renderer_native COMMAND mmx_renderer_native_test)
    set_tests_properties(mmx_custom_renderer_native PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-native")
    set(mmx_row_plan_test_sources ${mmx_renderer_native_test_sources})
    list(REMOVE_ITEM mmx_row_plan_test_sources tests/mmx_native_renderer_test.c)
    add_executable(mmx_row_plan_test ${mmx_row_plan_test_sources}
        tests/mmx_row_plan_test.c src/mmx_boss_rush_score.cpp)
    target_include_directories(mmx_row_plan_test PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    target_compile_features(mmx_row_plan_test PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(mmx_row_plan_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_row_plan_test PRIVATE -UNDEBUG)
    endif()
    if(NOT WIN32)
        target_link_libraries(mmx_row_plan_test PRIVATE m)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-row-plan")
    add_test(NAME mmx_custom_renderer_row_plan COMMAND mmx_row_plan_test)
    set_tests_properties(mmx_custom_renderer_row_plan PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-row-plan")
    foreach(reference FULLCOPY TRACE)
        string(TOLOWER "${reference}" reference_name)
        add_executable(mmx_renderer_${reference_name}_test
            ${mmx_renderer_reference_sources} tests/mmx_renderer_variant.c
            src/mmx_boss_rush_score.cpp)
        target_include_directories(mmx_renderer_${reference_name}_test PRIVATE
            src ${SNESRECOMP_ROOT}/runner/src)
        target_compile_definitions(mmx_renderer_${reference_name}_test PRIVATE
            MMX_RENDERER_TEST_${reference}=1)
        target_compile_features(mmx_renderer_${reference_name}_test PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(mmx_renderer_${reference_name}_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(mmx_renderer_${reference_name}_test PRIVATE -UNDEBUG)
        endif()
        if(NOT WIN32)
            target_link_libraries(mmx_renderer_${reference_name}_test PRIVATE m)
        endif()
        add_test(NAME mmx_custom_renderer_${reference_name}
            COMMAND mmx_renderer_${reference_name}_test)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-${reference_name}")
        set_tests_properties(mmx_custom_renderer_${reference_name} PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-${reference_name}")
    endforeach()
    add_executable(mmx_render_capture tools/mmx_render_capture.c src/mmx_renderer.c src/mmx_render_assets.c src/mmx_display.c src/mmx_wide_policy.c src/mmx_zero.c src/mmx_armor.c src/mmx_weapons.c src/mmx_weapon_combat.c src/mmx_coop_view.c src/mmx_boss_rush.c src/mmx_boss_rush_draw.c)
    target_include_directories(mmx_render_capture PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    foreach(mmx_render mmx_renderer_test mmx_render_capture)
        target_sources(${mmx_render} PRIVATE src/mmx_boss_rush_score.cpp)
        target_compile_features(${mmx_render} PRIVATE cxx_std_17)
    endforeach()
    get_target_property(mmx_capture_native_sources mmx_render_capture SOURCES)
    list(REMOVE_ITEM mmx_capture_native_sources src/mmx_renderer.c)
    add_executable(mmx_render_capture_native ${mmx_capture_native_sources} src/mmx_renderer_native.c)
    target_include_directories(mmx_render_capture_native PRIVATE src ${SNESRECOMP_ROOT}/runner/src)
    target_compile_features(mmx_render_capture_native PRIVATE cxx_std_17)
    target_compile_definitions(mmx_render_capture_native PRIVATE MMX_NATIVE_RASTER=1)
    foreach(mmx_test mmx_boss_rush_test mmx_boss_rush_score_test mmx_msu1_test mmx_display_test mmx_wide_policy_test mmx_renderer_test mmx_password_save_test mmx_zero_test)
        if(MSVC)
            target_compile_options(${mmx_test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${mmx_test} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    if(NOT WIN32)
        target_link_libraries(mmx_renderer_test PRIVATE m)
        target_link_libraries(mmx_render_capture PRIVATE m)
        target_link_libraries(mmx_render_capture_native PRIVATE m)
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
    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc")
        set(rush_report_dir "${CMAKE_CURRENT_BINARY_DIR}/boss-rush-reports")
        file(MAKE_DIRECTORY "${rush_report_dir}")
        add_test(NAME mmx_boss_rush_reports
            COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy MMX_REPLAY_RECORD=0
                MMX_BOSS_RUSH_TEST=1 MMX_BOSS_RUSH_REPORT_TEST=1
                $<TARGET_FILE:mmx_state_tests> "${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc")
        set_tests_properties(mmx_boss_rush_reports PROPERTIES
            WORKING_DIRECTORY "${rush_report_dir}" LABELS "rom;boss-rush")
        set(rush_pause_spawn_dir "${CMAKE_CURRENT_BINARY_DIR}/boss-rush-pause-spawn")
        file(MAKE_DIRECTORY "${rush_pause_spawn_dir}")
        add_test(NAME mmx_boss_rush_pause_spawn
            COMMAND ${CMAKE_COMMAND} -E env SDL_AUDIODRIVER=dummy MMX_REPLAY_RECORD=0
                MMX_BOSS_RUSH_TEST=1 MMX_BOSS_RUSH_PAUSE_SPAWN_TEST=1
                $<TARGET_FILE:mmx_state_tests> "${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc")
        set_tests_properties(mmx_boss_rush_pause_spawn PROPERTIES
            WORKING_DIRECTORY "${rush_pause_spawn_dir}" LABELS "rom;boss-rush")
        foreach(carpet_case native widescreen ultrawide no_shot)
            set(carpet_env)
            if(carpet_case STREQUAL "widescreen")
                list(APPEND carpet_env MMX_MAGIC_CARPET_WIDE=1)
            elseif(carpet_case STREQUAL "ultrawide")
                list(APPEND carpet_env MMX_MAGIC_CARPET_WIDE=32:9)
            elseif(carpet_case STREQUAL "no_shot")
                list(APPEND carpet_env MMX_MAGIC_CARPET_WIDE=1 MMX_MAGIC_CARPET_NO_SHOT=1)
            endif()
            set(carpet_dir "${CMAKE_CURRENT_BINARY_DIR}/magic-carpet-${carpet_case}")
            file(MAKE_DIRECTORY "${carpet_dir}")
            add_test(NAME mmx_magic_carpet_${carpet_case}
                COMMAND ${CMAKE_COMMAND} -E env
                    --unset=MMX_MAGIC_CARPET_WIDE --unset=MMX_MAGIC_CARPET_NO_SHOT
                    SDL_AUDIODRIVER=dummy MMX_REPLAY_RECORD=0
                    MMX_RASTER_INVARIANTS=1
                    "MMX_MAGIC_CARPET_TEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/magic_carpet_inputs.csv"
                    ${carpet_env} $<TARGET_FILE:mmx_state_tests>
                    "${CMAKE_CURRENT_SOURCE_DIR}/mmx.sfc")
            set_tests_properties(mmx_magic_carpet_${carpet_case} PROPERTIES
                WORKING_DIRECTORY "${carpet_dir}" LABELS "rom;magic-carpet")
        endforeach()
    endif()
endif()
include(cmake/saber.cmake)
