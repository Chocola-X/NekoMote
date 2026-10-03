add_library(nekomote_runtime STATIC deformation.cpp geometry_scene.cpp animation_curve.cpp animation.cpp)
add_library(nekomote::runtime ALIAS nekomote_runtime)
target_link_libraries(nekomote_runtime PUBLIC nekomote::model)
