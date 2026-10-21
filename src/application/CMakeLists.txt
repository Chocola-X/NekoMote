set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

add_library(nekomote_application STATIC session.cpp catalog.cpp controller.cpp config_io.cpp startup_config.cpp)
add_library(nekomote::application ALIAS nekomote_application)
target_link_libraries(nekomote_application PUBLIC nekomote::runtime)
target_link_libraries(nekomote_application PRIVATE nekomote::asset_io nlohmann_json::nlohmann_json
    Threads::Threads)
target_compile_features(nekomote_application PUBLIC cxx_std_20)
