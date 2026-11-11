# Diagnostic injection: pass this file through CMAKE_PROJECT_nekomote_INCLUDE
# to build the same workload against a frozen checkout without editing it.
add_executable(nekomote_ui_menu_probe ${CMAKE_CURRENT_LIST_DIR}/ui_menu_probe.cpp)
target_link_libraries(nekomote_ui_menu_probe PRIVATE
    nekomote::render_gl nekomote::ui glfw nekomote_gl_loader nekomote_warnings)
target_compile_features(nekomote_ui_menu_probe PRIVATE cxx_std_20)
set_target_properties(nekomote_ui_menu_probe PROPERTIES CXX_EXTENSIONS OFF)
