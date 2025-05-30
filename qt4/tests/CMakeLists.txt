add_definitions(${QT4_DEFINITIONS})
add_definitions(-DTESTDATADIR=\"${TESTDATADIR}\")

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_BINARY_DIR}
  ${QT4_INCLUDE_DIR}
)

macro(QT4_ADD_SIMPLETEST exe source)
  string(REPLACE "-" "" test_name ${exe})
  set(${test_name}_SOURCES
    ${source}
  )
  poppler_add_test(${exe} BUILD_TESTS ${${test_name}_SOURCES})
  target_link_libraries(${exe} poppler-qt4)
  if(MSVC)
    target_link_libraries(${exe} poppler ${poppler_LIBS})
  endif()
endmacro(QT4_ADD_SIMPLETEST)

macro(QT4_ADD_QTEST exe source)
  if (QT4_QTTEST_FOUND)
    string(REPLACE "-" "" test_name ${exe})
    set(${test_name}_SOURCES
      ${source}
    )
    poppler_add_unittest(${exe} BUILD_TESTS ${${test_name}_SOURCES})
    set(CMAKE_AUTOMOC ON)
    target_link_libraries(${exe} poppler-qt4 ${QT4_QTTEST_LIBRARY})
    if(MSVC)
      target_link_libraries(${exe} poppler ${poppler_LIBS})
    endif()
  endif ()
endmacro(QT4_ADD_QTEST)


qt4_add_simpletest(test-poppler-qt4 test-poppler-qt4.cpp)
qt4_add_simpletest(test-password-qt4 test-password-qt4.cpp)
qt4_add_simpletest(test-render-to-file-qt4 test-render-to-file.cpp)
qt4_add_simpletest(poppler-qt4-forms poppler-forms.cpp)
qt4_add_simpletest(poppler-qt4-fonts poppler-fonts.cpp)
qt4_add_simpletest(poppler-qt4-attachments poppler-attachments.cpp)
qt4_add_simpletest(stress-poppler-qt4 stress-poppler-qt4.cpp)
qt4_add_simpletest(stress-poppler-dir-qt4 stress-poppler-dir.cpp)
qt4_add_simpletest(stress-threads-qt4 stress-threads-qt4.cpp)
qt4_add_simpletest(poppler-qt4-texts poppler-texts.cpp)

qt4_add_qtest(check_qt4_attachments check_attachments.cpp)
qt4_add_qtest(check_qt4_dateConversion check_dateConversion.cpp)
qt4_add_qtest(check_qt4_fonts check_fonts.cpp)
qt4_add_qtest(check_qt4_links check_links.cpp)
qt4_add_qtest(check_qt4_metadata check_metadata.cpp)
qt4_add_qtest(check_qt4_optcontent check_optcontent.cpp)
qt4_add_qtest(check_qt4_pagelayout check_pagelayout.cpp)
qt4_add_qtest(check_qt4_pagemode check_pagemode.cpp)
qt4_add_qtest(check_qt4_password check_password.cpp)
qt4_add_qtest(check_qt4_permissions check_permissions.cpp)
qt4_add_qtest(check_qt4_search check_search.cpp)
qt4_add_qtest(check_qt4_actualtext check_actualtext.cpp)
qt4_add_qtest(check_qt4_lexer check_lexer.cpp)
qt4_add_qtest(check_qt4_pagelabelinfo check_pagelabelinfo.cpp)
qt4_add_qtest(check_qt4_goostring check_goostring.cpp)
if (NOT WIN32)
  qt4_add_qtest(check_qt4_strings check_strings.cpp)
endif ()
