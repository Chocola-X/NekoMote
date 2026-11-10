add_executable(nekomote_plasma_translation_test
    ${CMAKE_CURRENT_LIST_DIR}/../../tests/plasma/test_translations.cpp)
target_link_libraries(nekomote_plasma_translation_test PRIVATE
    Qt${NEKOMOTE_QT_MAJOR}::Quick nekomote_warnings)
add_dependencies(nekomote_plasma_translation_test nekomote_plasma_stage)
foreach(locale en_US zh_CN zh_TW)
    add_test(NAME plasma_translations_${locale} COMMAND nekomote_plasma_translation_test
        ${NEKOMOTE_STAGE_DIR}/${NEKOMOTE_INSTALL_QMLDIR}
        ${NEKOMOTE_STAGE_DIR}/share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/config.qml
        ${locale})
    set_tests_properties(plasma_translations_${locale} PROPERTIES LABELS "qt" TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
