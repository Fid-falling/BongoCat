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

add_test(NAME preferences-navigation COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesNavigationAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat_neo>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-navigation-test)
set_tests_properties(preferences-navigation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME preferences-slider-drag COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesSliderDragAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat_neo>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-slider-drag-test)
set_tests_properties(preferences-slider-drag PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME preferences-model-border COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesModelBorderAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat_neo>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-model-border-test)
set_tests_properties(preferences-model-border PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
