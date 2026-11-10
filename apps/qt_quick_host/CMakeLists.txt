# Optional host dependency only; Qt does not enter the core/ABI public headers.
# The shared Qt bridge lives in src/host_qt_quick and is linked here.
add_executable(nekomote_qt_quick_host main.cpp)
set_target_properties(nekomote_qt_quick_host PROPERTIES AUTOMOC ON)
target_link_libraries(nekomote_qt_quick_host PRIVATE
    nekomote::host_qt_quick Qt${NEKOMOTE_QT_MAJOR}::Quick Qt${NEKOMOTE_QT_MAJOR}::OpenGL nekomote_warnings)

# Model-free reproducer for host submissions caused by Qt's FBO scheduling.
add_executable(nekomote_qt_fbo_probe ../../tools/perf_compare/qt_fbo_probe.cpp)
target_link_libraries(nekomote_qt_fbo_probe PRIVATE
    Qt${NEKOMOTE_QT_MAJOR}::Quick Qt${NEKOMOTE_QT_MAJOR}::OpenGL nekomote_warnings)
