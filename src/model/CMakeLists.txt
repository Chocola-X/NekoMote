cmake_minimum_required(VERSION 3.25)

# nekomote_model: the window-free core. Strongly typed ids, math primitives and
# unit/coordinate conventions. Allowed deps: C++ standard library only
# (AGENTS.md §3). No json, no OpenGL, no window lib, no filesystem.
add_library(nekomote_model STATIC)
add_library(nekomote::model ALIAS nekomote_model)

target_sources(nekomote_model
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/model.cpp
)

target_include_directories(nekomote_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(nekomote_model PUBLIC cxx_std_20)
target_link_libraries(nekomote_model PUBLIC nekomote_warnings)
