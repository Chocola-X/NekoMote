# Optional host dependency only; Qt does not enter the core/ABI public headers.
# The shared Qt bridge lives in src/host_qt_quick and is linked here.
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick OpenGL)
add_executable(nekomote_qt_quick_host main.cpp)
set_target_properties(nekomote_qt_quick_host PROPERTIES AUTOMOC ON)
target_link_libraries(nekomote_qt_quick_host PRIVATE
    nekomote::host_qt_quick Qt6::Quick Qt6::OpenGL nekomote_warnings)
