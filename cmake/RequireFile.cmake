if(NOT DEFINED REQUIRED_FILE OR REQUIRED_FILE STREQUAL "")
  message(FATAL_ERROR "REQUIRED_FILE must name the expected package file")
endif()

if(NOT EXISTS "${REQUIRED_FILE}")
  message(FATAL_ERROR "Required package file was not generated: ${REQUIRED_FILE}")
endif()
