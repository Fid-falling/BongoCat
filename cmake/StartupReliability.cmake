add_test(NAME startup-soak COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
  -File ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StartupSoak.ps1
  -Exe $<TARGET_FILE:bongo_cat_neo>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/startup-soak-test)
set_tests_properties(startup-soak PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME startup-reliability-matrix COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StartupMatrixAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat_neo>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/startup-matrix-test)
set_tests_properties(startup-reliability-matrix PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
