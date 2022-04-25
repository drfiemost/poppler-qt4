
if (ENABLE_SPLASH)

  if (HAVE_NANOSLEEP OR LIB_RT_HAS_NANOSLEEP)
    set (perf_test_SRCS
      perf-test.cc
      perf-test-preview-dummy.cc
    )
    add_executable(perf-test ${perf_test_SRCS})
    target_link_libraries(perf-test  $<TARGET_OBJECTS:poppler> ${poppler_LIBS})
    if (LIB_RT_HAS_NANOSLEEP)
      target_link_libraries(perf-test rt)
    endif ()
  endif ()

endif ()

set (pdf_fullrewrite_SRCS
  pdf-fullrewrite.cc
  parseargs.cc
)
add_executable(pdf-fullrewrite ${pdf_fullrewrite_SRCS})
target_link_libraries(pdf-fullrewrite $<TARGET_OBJECTS:poppler> ${poppler_LIBS})
