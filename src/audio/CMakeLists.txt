find_package(PkgConfig REQUIRED)
# The device uses the established mainloop/context/stream API, available on focal.
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET "libpulse>=13.99")
find_package(Threads REQUIRED)
add_library(nekomote_audio STATIC device.cpp)
add_library(nekomote::audio ALIAS nekomote_audio)
target_include_directories(nekomote_audio PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nekomote_audio PUBLIC nekomote::application PRIVATE PkgConfig::PULSE Threads::Threads)
