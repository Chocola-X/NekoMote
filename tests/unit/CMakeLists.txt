cmake_minimum_required(VERSION 3.25)

# Catch2 is fetched via FetchContent in the top-level CMakeLists.txt, which
# defines the Catch2::Catch2WithMain target and ships Catch.cmake under its
# source tree. We do not call find_package(Catch2) because FetchContent does
# not install a Catch2Config.cmake into a path find_package searches by default.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/cmake)

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
    test_chain.cpp)

target_link_libraries(nekomote_unit_tests
    PRIVATE
        nekomote::asset_io
        nekomote::runtime
        nekomote::application
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json)

target_compile_features(nekomote_unit_tests PRIVATE cxx_std_20)

target_compile_definitions(nekomote_unit_tests PRIVATE
    NEKOMOTE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

include(Catch)
catch_discover_tests(nekomote_unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# C ABI test (pure C, verifies the C boundary is usable without C++).
# Uses a real model asset if available; skipped otherwise.
if(NEKOMOTE_BUILD_PREVIEW)
    add_executable(nekomote_abi_test test_abi.c)
    target_link_libraries(nekomote_abi_test PRIVATE nekomote::abi)
    target_compile_features(nekomote_abi_test PRIVATE c_std_11)
    target_compile_options(nekomote_abi_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2)
    set(NEKOMOTE_ABI_TEST_MODEL "${CMAKE_SOURCE_DIR}/assets/characters/chocola-date-a/model.nkm.json")
    set(NEKOMOTE_ABI_TEST_SESSION "${CMAKE_SOURCE_DIR}/assets/characters/chocola-date-a/session.nks.json")
    add_test(NAME abi_test
        COMMAND nekomote_abi_test "${NEKOMOTE_ABI_TEST_MODEL}" "${NEKOMOTE_ABI_TEST_SESSION}"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
