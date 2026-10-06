add_library(nekomote_abi STATIC abi.cpp)
add_library(nekomote::abi ALIAS nekomote_abi)
target_include_directories(nekomote_abi PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(nekomote_abi
    PUBLIC
        nekomote::application
        nekomote::asset_io
    PRIVATE
        nekomote::render_gl
        nekomote_warnings)
target_compile_features(nekomote_abi PUBLIC cxx_std_20)
