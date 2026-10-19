cmake_minimum_required(VERSION 3.25)
find_package(ZLIB 1.2.11 REQUIRED)

# The dependency provider supplies Catch2 targets and its CTest discovery module.
add_executable(nekomote_unit_tests
    test_types.cpp
    test_math.cpp
    test_units.cpp
    test_import.cpp
    test_asset_validation.cpp
    test_image.cpp
    test_audio.cpp
    test_deformation.cpp
    test_geometry_scene.cpp
    test_animation_curve.cpp
    test_animation.cpp
    test_session.cpp
    test_session_asset.cpp
    test_spring.cpp
    test_bend.cpp
    test_chain.cpp
    test_controller.cpp
    test_ui_layout.cpp
    test_ui_menu.cpp)

target_link_libraries(nekomote_unit_tests
    PRIVATE
        nekomote::importer
        nekomote::runtime
        nekomote::application
        nekomote::ui
        Catch2::Catch2WithMain
        ${NEKOMOTE_FLAC_TARGET}
        nlohmann_json::nlohmann_json)

target_compile_features(nekomote_unit_tests PRIVATE cxx_std_20)
target_include_directories(nekomote_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/asset_io)
target_link_libraries(nekomote_unit_tests PRIVATE ZLIB::ZLIB)

target_compile_definitions(nekomote_unit_tests PRIVATE
    NEKOMOTE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

include(Catch)
# Catch 3.5.3 does not map tags to CTest labels. Discover the groups separately
# so asset/font integration tests stay outside the mandatory headless CI set.
# Match the all-skipped report instead of exit code 4: Catch also returns 4
# for four failed assertions, which must remain a failure.
set(NEKOMOTE_CATCH_SKIP_REPORT "test cases: 1 [|] 1 skipped")
if(TARGET nekomote_audio)
    add_executable(nekomote_audio_device_tests test_audio_device.cpp)
    target_link_libraries(nekomote_audio_device_tests PRIVATE nekomote::audio
        Catch2::Catch2WithMain nekomote_warnings)
    add_test(NAME audio_device_contract COMMAND nekomote_audio_device_tests)
    # Bank lifetime must be independent of an audio server, including teardown
    # while connecting. Never send these synthetic clips to the user's device.
    set_tests_properties(audio_device_contract PROPERTIES
        ENVIRONMENT "PULSE_SERVER=unix:/nekomote-unit-tests/no-pulse-server" TIMEOUT 15)
endif()
catch_discover_tests(nekomote_unit_tests
    TEST_SPEC "~[integration]"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    EXTRA_ARGS --colour-mode none
    PROPERTIES SKIP_REGULAR_EXPRESSION "${NEKOMOTE_CATCH_SKIP_REPORT}")
catch_discover_tests(nekomote_unit_tests
    TEST_SPEC "[integration]"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    EXTRA_ARGS --colour-mode none
    PROPERTIES LABELS integration
               SKIP_REGULAR_EXPRESSION "${NEKOMOTE_CATCH_SKIP_REPORT}")

# C ABI tests (pure C, verifies the C boundary is usable without C++).
if(NEKOMOTE_BUILD_ABI)
    # Negative tests: no assets, no GPU, safe for ASan/headless/LSan.
    # Verifies NULL rejection, error diagnostics and failed-init RAII.
    # Live-handle input checks use the synthetic test below and GPU tests.
    add_executable(nekomote_abi_negative_test test_abi_negative.c)
    target_link_libraries(nekomote_abi_negative_test PRIVATE nekomote::abi)
    target_compile_features(nekomote_abi_negative_test PRIVATE c_std_11)
    target_compile_options(nekomote_abi_negative_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2)
    add_test(NAME abi_negative_test
        COMMAND nekomote_abi_negative_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Positive tests: uses a real model asset if available; skipped otherwise.
    add_executable(nekomote_abi_test test_abi.c)
    target_link_libraries(nekomote_abi_test PRIVATE nekomote::abi)
    target_compile_features(nekomote_abi_test PRIVATE c_std_11)
    target_compile_options(nekomote_abi_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2)
    set(NEKOMOTE_ABI_TEST_MODEL "${CMAKE_SOURCE_DIR}/assets/characters/chocola/date-a/model.nkm")
    set(NEKOMOTE_ABI_TEST_SESSION "${CMAKE_SOURCE_DIR}/assets/characters/chocola/date-a/session.nks.json")
    add_test(NAME abi_test
        COMMAND nekomote_abi_test "${NEKOMOTE_ABI_TEST_MODEL}" "${NEKOMOTE_ABI_TEST_SESSION}"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(abi_test PROPERTIES SKIP_RETURN_CODE 77 LABELS integration)
endif()

if(NEKOMOTE_BUILD_ABI)
    add_executable(nekomote_abi_boundary_tests
        test_abi_boundary.cpp test_abi_allocations.cpp test_wallpaper_config_path.cpp)
    target_link_libraries(nekomote_abi_boundary_tests PRIVATE nekomote::abi nekomote::importer
        Catch2::Catch2WithMain nlohmann_json::nlohmann_json nekomote_warnings)
    target_compile_definitions(nekomote_abi_boundary_tests PRIVATE
        NEKOMOTE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME abi_synthetic_boundary COMMAND nekomote_abi_boundary_tests)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
# Separate processes keep FONTCONFIG_FILE and font caches isolated from the suite.
if(Python3_Interpreter_FOUND)
    add_executable(nekomote_font_probe font_probe.cpp)
    target_link_libraries(nekomote_font_probe PRIVATE nekomote::ui nekomote_warnings)
    foreach(font_case empty custom retry glyph cjk)
        add_test(NAME ui_font_${font_case}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ui/check_font_discovery.py
                $<TARGET_FILE:nekomote_font_probe> ${font_case})
        set_tests_properties(ui_font_${font_case} PROPERTIES TIMEOUT 30)
        if(NOT font_case STREQUAL "empty")
            set_tests_properties(ui_font_${font_case} PROPERTIES
                LABELS integration SKIP_RETURN_CODE 77)
        endif()
    endforeach()
endif()
