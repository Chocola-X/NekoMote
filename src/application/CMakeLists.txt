add_library(nekomote_application STATIC session.cpp)
add_library(nekomote::application ALIAS nekomote_application)
target_link_libraries(nekomote_application PUBLIC nekomote::runtime)
target_compile_features(nekomote_application PUBLIC cxx_std_20)
