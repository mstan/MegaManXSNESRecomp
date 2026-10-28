set(_saber_plugin_source "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/mmx_saber_plugin.c")
set(_saber_assets_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_assets.c")
set(_saber_wave_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_wave.c")
set(_saber_wave_runtime_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_wave_runtime.c")
set(_saber_wave_assets_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_wave_assets.c")
set(_saber_sfx_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_sfx.c")
set(_saber_input_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_input.c")
set(_saber_combo_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_combo.c")
set(_saber_attack_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_attack.c")
set(_saber_priority_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_priority.c")
set(_saber_frame_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_frame.c")
set(_saber_hitbox_debug_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_hitbox_debug.c")
set(_saber_render_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_render.c")
set(_saber_tuning_source "${CMAKE_CURRENT_SOURCE_DIR}/src/saber/mmx_saber_tuning.c")
set(_saber_sfx_converter_source "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/mmx_saber_sfx_convert.c")
set(_saber_sfx_codec_source "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/mmx_saber_sfx_codec.c")
set(_saber_sfx_codec_header "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/mmx_saber_sfx_codec.h")
set(_saber_sfx_vorbis_source "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/third_party/stb_vorbis.c")
set(_saber_sfx_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/saber-zero/sfx")
set(_saber_converter "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/convert_saber_zero.py")
set(_saber_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/saber_zero_manifest.json")
set(_ride_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/ride_zero_manifest.json")
set(_saber_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/saber-zero/sprites")
set(_saber_cache_dir "${CMAKE_CURRENT_BINARY_DIR}/cache/mmx-source")
set(_saber_cache "${_saber_cache_dir}/saber-v1.bin")
set(_ride_cache "${_saber_cache_dir}/ride-zero-v1.bin")
set(_saber_sfx_cache "${_saber_cache_dir}/saber-sfx-v2.bin")
set(_saber_wave_test_cache_dir "${_saber_cache_dir}")
file(GLOB _saber_sfx_inputs CONFIGURE_DEPENDS
    "${_saber_sfx_source_dir}/*.ogg")

add_executable(mmx_saber_sfx_convert
    "${_saber_sfx_converter_source}" "${_saber_sfx_codec_source}"
    "${_saber_sfx_vorbis_source}")
target_include_directories(mmx_saber_sfx_convert PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber")
if(NOT WIN32)
    target_link_libraries(mmx_saber_sfx_convert PRIVATE m)
endif()

# The donor art and sound are tracked in the repository and the plugin is
# always compiled in, so missing inputs mean a broken checkout. Fail the build
# rather than ship an executable whose Saber Zero cannot load.
if(NOT (EXISTS "${_saber_sfx_source_dir}/saber_1.ogg" AND
        EXISTS "${_saber_sfx_source_dir}/saber_2.ogg" AND
        EXISTS "${_saber_sfx_source_dir}/saber_3.ogg" AND _saber_sfx_inputs))
    message(FATAL_ERROR "Saber Zero sound sources missing from ${_saber_sfx_source_dir}")
endif()
add_custom_command(
    OUTPUT "${_saber_sfx_cache}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_saber_cache_dir}"
    COMMAND "$<TARGET_FILE:mmx_saber_sfx_convert>"
        --source-dir "${_saber_sfx_source_dir}"
        --out "${_saber_sfx_cache}"
    DEPENDS mmx_saber_sfx_convert ${_saber_sfx_inputs}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_target(saber_sfx_cache DEPENDS "${_saber_sfx_cache}")

# The tracked donor sheets build the same deterministic v1 sidecars as the old
# branch. The build tree keeps them as files for the format tests; the game
# gets them compiled in (below), so no release has to stage them.
file(GLOB _saber_donor_files CONFIGURE_DEPENDS
    "${_saber_source_dir}/*.png")
if(NOT (EXISTS "${_saber_source_dir}" AND _saber_donor_files AND
        EXISTS "${_saber_manifest}" AND EXISTS "${_ride_manifest}"))
    message(FATAL_ERROR "Saber Zero donor sprites or manifests missing from ${_saber_source_dir}")
endif()
add_custom_command(
    OUTPUT "${_saber_cache}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_saber_cache_dir}"
    COMMAND "${Python3_EXECUTABLE}" -I "${_saber_converter}"
        --manifest "${_saber_manifest}"
        --source-dir "${_saber_source_dir}"
        --out "${_saber_cache}"
    DEPENDS "${_saber_converter}" "${_saber_manifest}" ${_saber_donor_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_command(
    OUTPUT "${_ride_cache}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_saber_cache_dir}"
    COMMAND "${Python3_EXECUTABLE}" -I "${_saber_converter}"
        --manifest "${_ride_manifest}"
        --source-dir "${_saber_source_dir}"
        --out "${_ride_cache}"
    DEPENDS "${_saber_converter}" "${_ride_manifest}" ${_saber_donor_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_target(saber_asset_caches DEPENDS "${_saber_cache}" "${_ride_cache}")

# Compile the three donor-built sidecars into every target that links the
# plugin. The ROM-derived X3 wave is not here: it is extracted at runtime from
# the player's own X3 ROM.
set(_saber_embedded_source "${CMAKE_CURRENT_BINARY_DIR}/generated/mmx_saber_embedded.c")
add_custom_command(
    OUTPUT "${_saber_embedded_source}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUT=${_saber_embedded_source}"
        "-DSABER=${_saber_cache}" "-DRIDE=${_ride_cache}"
        "-DSFX=${_saber_sfx_cache}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_saber_sidecars.cmake"
    DEPENDS "${_saber_cache}" "${_ride_cache}" "${_saber_sfx_cache}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_saber_sidecars.cmake"
    VERBATIM)
add_custom_target(saber_embedded_source DEPENDS "${_saber_embedded_source}")

if(TARGET MegaManXSNESRecomp)
    target_sources(MegaManXSNESRecomp PRIVATE
        "${_saber_embedded_source}" "${_saber_plugin_source}" "${_saber_assets_source}"
        "${_saber_wave_source}" "${_saber_wave_assets_source}"
        "${_saber_wave_runtime_source}"
        "${_saber_sfx_source}" "${_saber_input_source}"
        "${_saber_combo_source}"
        "${_saber_attack_source}"
        "${_saber_priority_source}"
        "${_saber_frame_source}" "${_saber_hitbox_debug_source}"
        "${_saber_render_source}"
        "${_saber_tuning_source}")
    add_dependencies(MegaManXSNESRecomp saber_embedded_source)
endif()

if(MMX_STATE_TESTS AND TARGET mmx_state_tests)
    target_sources(mmx_state_tests PRIVATE
        "${_saber_embedded_source}" "${_saber_plugin_source}" "${_saber_assets_source}"
        "${_saber_wave_source}" "${_saber_wave_assets_source}"
        "${_saber_wave_runtime_source}"
        "${_saber_sfx_source}" "${_saber_input_source}"
        "${_saber_combo_source}"
        "${_saber_attack_source}"
        "${_saber_priority_source}"
        "${_saber_frame_source}" "${_saber_hitbox_debug_source}"
        "${_saber_render_source}"
        "${_saber_tuning_source}")
    add_dependencies(mmx_state_tests saber_embedded_source)
    get_target_property(_saber_sources mmx_state_tests SOURCES)
    list(REMOVE_ITEM _saber_sources
        tests/mmx_adaptive_state_test.c
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mmx_adaptive_state_test.c"
        src/mods/mmx_saber_plugin.c
        "${_saber_plugin_source}"
        src/saber/mmx_saber_assets.c
        "${_saber_assets_source}"
        src/saber/mmx_saber_wave.c
        "${_saber_wave_source}"
        src/saber/mmx_saber_wave_runtime.c
        "${_saber_wave_runtime_source}"
        src/saber/mmx_saber_wave_assets.c
        "${_saber_wave_assets_source}"
        src/saber/mmx_saber_sfx.c
        "${_saber_sfx_source}"
        src/saber/mmx_saber_attack.c
        "${_saber_attack_source}"
        src/saber/mmx_saber_priority.c
        "${_saber_priority_source}"
        src/saber/mmx_saber_render.c
        "${_saber_render_source}"
        src/saber/mmx_saber_hitbox_debug.c
        "${_saber_hitbox_debug_source}"
        src/saber/mmx_saber_tuning.c
        "${_saber_tuning_source}")

    add_executable(mmx_saber_rom_tests
        tests/saber/saber_rom_test.c ${_saber_sources}
        "${_saber_plugin_source}" "${_saber_assets_source}"
        "${_saber_wave_source}" "${_saber_wave_assets_source}"
        "${_saber_wave_runtime_source}"
        "${_saber_sfx_source}" "${_saber_input_source}"
        "${_saber_combo_source}"
        "${_saber_attack_source}"
        "${_saber_priority_source}"
        "${_saber_frame_source}" "${_saber_hitbox_debug_source}"
        "${_saber_render_source}"
        "${_saber_tuning_source}")
    foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
        get_target_property(_value mmx_state_tests ${_property})
        if(_value)
            set_property(TARGET mmx_saber_rom_tests PROPERTY ${_property} "${_value}")
        endif()
    endforeach()
    add_dependencies(mmx_saber_rom_tests MegaManXSNESRecomp_widescreen_overrides)
    add_dependencies(mmx_saber_rom_tests saber_embedded_source)
endif()

if(BUILD_TESTING)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")

    add_executable(mmx_saber_input_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_input_test.c"
        "${_saber_input_source}")
    target_include_directories(mmx_saber_input_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber")
    add_test(NAME mmx_saber_input COMMAND mmx_saber_input_test)

    add_executable(mmx_saber_tuning_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_tuning_test.c"
        "${_saber_tuning_source}")
    target_include_directories(mmx_saber_tuning_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber")
    add_test(NAME mmx_saber_tuning COMMAND mmx_saber_tuning_test)

    add_executable(mmx_saber_priority_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_priority_test.c"
        "${_saber_priority_source}" "${_saber_tuning_source}")
    target_include_directories(mmx_saber_priority_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber")
    add_test(NAME mmx_saber_priority COMMAND mmx_saber_priority_test)

    add_executable(mmx_saber_sfx_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_sfx_test.c"
        "${_saber_sfx_source}")
    target_include_directories(mmx_saber_sfx_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber")
    target_compile_definitions(mmx_saber_sfx_test PRIVATE
        MMX_SABER_SFX_CACHE="${_saber_sfx_cache}")
    add_test(NAME mmx_saber_sfx_loader COMMAND mmx_saber_sfx_test)
    set_tests_properties(mmx_saber_sfx_loader PROPERTIES SKIP_RETURN_CODE 77)
    if(MSVC)
        target_compile_options(mmx_saber_sfx_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_saber_sfx_test PRIVATE -UNDEBUG)
    endif()
    if(TARGET saber_sfx_cache)
        add_dependencies(mmx_saber_sfx_test saber_sfx_cache)
    endif()

    add_executable(mmx_saber_sfx_codec_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_sfx_codec_test.c"
        "${_saber_sfx_codec_source}")
    target_include_directories(mmx_saber_sfx_codec_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber")
    add_test(NAME mmx_saber_sfx_codec COMMAND mmx_saber_sfx_codec_test)
    if(MSVC)
        target_compile_options(mmx_saber_sfx_codec_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_saber_sfx_codec_test PRIVATE -UNDEBUG)
    endif()
    if(NOT WIN32)
        target_link_libraries(mmx_saber_sfx_codec_test PRIVATE m)
    endif()

    add_test(NAME saber_sfx_converter
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/test_saber_sfx_converter.py"
            --converter $<TARGET_FILE:mmx_saber_sfx_convert>)
    set_tests_properties(saber_sfx_converter PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")

    add_test(NAME saber_asset_layout
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/test_saber_zero_assets.py")
    set_tests_properties(saber_asset_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")

    add_executable(mmx_saber_check
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/saber/mmx_saber_check.c"
        "${_saber_assets_source}"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_include_directories(mmx_saber_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
        "${SNESRECOMP_ROOT}/runner/src")

    add_executable(mmx_saber_assets_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_assets_test.c"
        "${_saber_assets_source}" "${_saber_sfx_source}"
        "${_saber_embedded_source}"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_include_directories(mmx_saber_assets_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
        "${SNESRECOMP_ROOT}/runner/src")
    target_compile_definitions(mmx_saber_assets_test PRIVATE
        MMX_SABER_CACHE_DIR="${_saber_cache_dir}")
    add_test(NAME mmx_saber_assets_test COMMAND mmx_saber_assets_test)
    add_dependencies(mmx_saber_check saber_asset_caches)
    add_dependencies(mmx_saber_assets_test saber_embedded_source)

    add_executable(mmx_saber_render_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_render_test.c"
        "${_saber_render_source}" "${_saber_assets_source}"
        "${_saber_wave_source}"
        "${_saber_attack_source}" "${_saber_wave_runtime_source}" "${_saber_sfx_source}"
        "${_saber_priority_source}" "${_saber_tuning_source}"
        "${_saber_hitbox_debug_source}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_wide_policy.c"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_include_directories(mmx_saber_render_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
        "${SNESRECOMP_ROOT}/runner/src")
    target_compile_definitions(mmx_saber_render_test PRIVATE
        MMX_SABER_RENDER_CACHE_DIR="${_saber_cache_dir}")
    add_test(NAME mmx_saber_render COMMAND mmx_saber_render_test)
    set_tests_properties(mmx_saber_render PROPERTIES SKIP_RETURN_CODE 77)
    if(TARGET saber_asset_caches)
        add_dependencies(mmx_saber_render_test saber_asset_caches)
    endif()

    add_executable(mmx_saber_renderer_draw_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_renderer_draw_test.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_renderer.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_render_assets.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_display.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_wide_policy.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_zero.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_armor.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_weapons.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_weapon_combat.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_coop_view.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_boss_rush.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_boss_rush_draw.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mmx_boss_rush_score.cpp"
        "${_saber_assets_source}"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_compile_features(mmx_saber_renderer_draw_test PRIVATE cxx_std_17)
    target_include_directories(mmx_saber_renderer_draw_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
        "${SNESRECOMP_ROOT}/runner/src")
    target_compile_definitions(mmx_saber_renderer_draw_test PRIVATE
        MMX_SABER_RENDER_CACHE_DIR="${_saber_cache_dir}"
        MMX_SABER_DRAW_ZERO_PATH="${CMAKE_CURRENT_BINARY_DIR}/tmp/saber-render-draw-zero.bin"
        MMX_SABER_DRAW_WEAPON_PATH="${CMAKE_CURRENT_BINARY_DIR}/tmp/saber-render-draw-weapon.bin")
    add_test(NAME mmx_saber_renderer_draw COMMAND mmx_saber_renderer_draw_test)
    set_tests_properties(mmx_saber_renderer_draw PROPERTIES SKIP_RETURN_CODE 77)
    if(MSVC)
        target_compile_options(mmx_saber_renderer_draw_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(mmx_saber_renderer_draw_test PRIVATE -UNDEBUG)
    endif()
    if(NOT WIN32)
        target_link_libraries(mmx_saber_renderer_draw_test PRIVATE m)
    endif()
    if(TARGET saber_asset_caches)
        add_dependencies(mmx_saber_renderer_draw_test saber_asset_caches)
    endif()

    add_executable(mmx_saber_wave_test
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/saber/mmx_saber_wave_test.c"
        "${_saber_wave_source}" "${_saber_wave_assets_source}"
        "${SNESRECOMP_ROOT}/runner/src/sha256.c")
    target_include_directories(mmx_saber_wave_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/saber"
        "${SNESRECOMP_ROOT}/runner/src")
    target_compile_definitions(mmx_saber_wave_test PRIVATE
        MMX_SABER_WAVE_TEST_CACHE_DIR="${_saber_wave_test_cache_dir}")
    add_test(NAME mmx_saber_wave COMMAND mmx_saber_wave_test)
    set_tests_properties(mmx_saber_wave PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT "MMX_SABER_X3_ROM=${CMAKE_CURRENT_SOURCE_DIR}/Mega Man X3 (USA).sfc")

endif()
