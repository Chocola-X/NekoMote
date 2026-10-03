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
    test_asset_validation.cpp)

target_link_libraries(nekomote_unit_tests
    PRIVATE
        nekomote::asset_io
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json)

target_compile_features(nekomote_unit_tests PRIVATE cxx_std_20)

target_compile_definitions(nekomote_unit_tests PRIVATE
    NEKOMOTE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

include(Catch)
catch_discover_tests(nekomote_unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
