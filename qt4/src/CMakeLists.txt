
set(CMAKE_AUTOMOC ON)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${QT4_INCLUDE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

set(poppler_qt4_SRCS
  poppler-annotation.cc
  poppler-document.cc
  poppler-embeddedfile.cc
  poppler-fontinfo.cc
  poppler-form.cc
  poppler-link.cc
  poppler-link-extractor.cc
  poppler-movie.cc
  poppler-optcontent.cc
  poppler-page.cc
  poppler-base-converter.cc
  poppler-pdf-converter.cc
  poppler-private.cc
  poppler-ps-converter.cc
  poppler-qiodeviceoutstream.cc
  poppler-sound.cc
  poppler-textbox.cc
  poppler-page-transition.cc
  poppler-media.cc
  ArthurOutputDev.cc
)

add_library(poppler-qt4 SHARED ${poppler_qt4_SRCS})
set_target_properties(poppler-qt4 PROPERTIES VERSION 4.11.0 SOVERSION 4)
if(MINGW)
    get_target_property(POPPLER_QT4_SOVERSION poppler-qt4 SOVERSION)
    set_target_properties(poppler-qt4 PROPERTIES SUFFIX "-${POPPLER_QT4_SOVERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
target_link_libraries(poppler-qt4 poppler Qt4::QtCore Qt4::QtGui Qt4::QtXml ${poppler_LIBS})
install(TARGETS poppler-qt4
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES
  poppler-qt4.h
  poppler-link.h
  poppler-annotation.h
  poppler-form.h
  poppler-optcontent.h
  poppler-export.h
  poppler-page-transition.h
  poppler-media.h
  DESTINATION include/poppler/qt4)

