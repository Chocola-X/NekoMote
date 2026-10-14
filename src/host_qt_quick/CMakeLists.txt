# Shared Qt Quick bridge: used by the isolated diagnostic host and the QML
# wallpaper module. Depends only on the C ABI and Qt; the core never sees Qt.
add_library(nekomote_host_qt_quick STATIC session_item.cpp session_item.hpp)
add_library(nekomote::host_qt_quick ALIAS nekomote_host_qt_quick)
set_target_properties(nekomote_host_qt_quick PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)
target_include_directories(nekomote_host_qt_quick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekomote_host_qt_quick PUBLIC
    nekomote::abi Qt${NEKOMOTE_QT_MAJOR}::Quick Qt${NEKOMOTE_QT_MAJOR}::OpenGL nekomote_warnings)

if(NEKOMOTE_BUILD_VULKAN)
    target_sources(nekomote_host_qt_quick PRIVATE vulkan_gl_bridge.cpp vulkan_gl_bridge.hpp)
    target_compile_definitions(nekomote_host_qt_quick PRIVATE NEKOMOTE_HAS_VULKAN=1)
    target_link_libraries(nekomote_host_qt_quick PRIVATE Vulkan::Vulkan)
endif()
