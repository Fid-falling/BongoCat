if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE FILES
  "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
  "${ROOT}/src/*.cxx" "${ROOT}/src/*.m" "${ROOT}/src/*.mm"
  "${ROOT}/src/*.h" "${ROOT}/src/*.hh" "${ROOT}/src/*.hpp" "${ROOT}/src/*.hxx"
  "${ROOT}/include/*.h" "${ROOT}/include/*.hh"
  "${ROOT}/include/*.hpp" "${ROOT}/include/*.hxx"
  "${ROOT}/tests/*.c" "${ROOT}/tests/*.cc" "${ROOT}/tests/*.cpp"
  "${ROOT}/tests/*.cxx" "${ROOT}/tests/*.m" "${ROOT}/tests/*.mm"
  "${ROOT}/tests/*.h" "${ROOT}/tests/*.hh"
  "${ROOT}/tests/*.hpp" "${ROOT}/tests/*.hxx"
  "${ROOT}/cmake/*.cmake" "${ROOT}/cmake/*.ps1")
list(APPEND FILES "${ROOT}/CMakeLists.txt")

set(FAILED "")
foreach(FILE IN LISTS FILES)
  file(STRINGS "${FILE}" LINES)
  list(LENGTH LINES COUNT)
  if(COUNT GREATER 300)
    list(APPEND FAILED "${FILE}: ${COUNT}")
  endif()
endforeach()

if(FAILED)
  list(JOIN FAILED "\n" MESSAGE_TEXT)
  message(FATAL_ERROR "Files over 300 lines:\n${MESSAGE_TEXT}")
endif()

message(STATUS "300-line policy passed")
