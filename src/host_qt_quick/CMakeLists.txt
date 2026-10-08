# Shared Qt Quick bridge: used by the isolated diagnostic host and the QML
# wallpaper module. Depends only on the C ABI and Qt; the core never sees Qt.
find_package(Qt6 6.9.1 EXACT REQUIRED COMPONENTS Quick OpenGL)
add_library(nekomote_host_qt_quick STATIC session_item.cpp session_item.hpp)
add_library(nekomote::host_qt_quick ALIAS nekomote_host_qt_quick)
set_target_properties(nekomote_host_qt_quick PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)
target_include_directories(nekomote_host_qt_quick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekomote_host_qt_quick PUBLIC
    nekomote::abi Qt6::Quick Qt6::OpenGL nekomote_warnings)
