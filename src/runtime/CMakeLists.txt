add_library(nekomote_runtime STATIC deformation.cpp)
add_library(nekomote::runtime ALIAS nekomote_runtime)
target_link_libraries(nekomote_runtime PUBLIC nekomote::model)
