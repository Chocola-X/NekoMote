# Optional host dependency only; Qt does not enter the core/ABI public headers.
find_package(Qt6 6.9.1 EXACT REQUIRED COMPONENTS Quick OpenGL)
add_executable(nekomote_qt_quick_host main.cpp session_item.cpp session_item.hpp)
set_target_properties(nekomote_qt_quick_host PROPERTIES AUTOMOC ON)
target_link_libraries(nekomote_qt_quick_host PRIVATE
    nekomote::abi Qt6::Quick Qt6::OpenGL nekomote_warnings)
