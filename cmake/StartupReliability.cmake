add_test(NAME startup-soak COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
  -File ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StartupSoak.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/startup-soak-test)
set_tests_properties(startup-soak PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME startup-reliability-matrix COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StartupMatrixAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/startup-matrix-test)
set_tests_properties(startup-reliability-matrix PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

add_test(NAME preferences-navigation COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesNavigationAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-navigation-test)
set_tests_properties(preferences-navigation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME preferences-slider-drag COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesSliderDragAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-slider-drag-test)
set_tests_properties(preferences-slider-drag PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME preferences-model-border COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesModelBorderAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-model-border-test)
set_tests_properties(preferences-model-border PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_test(NAME screen-policy COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScreenPolicyAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/screen-policy-test)
set_tests_properties(screen-policy PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

add_test(NAME preferences-interaction COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesInteractionAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-interaction-test)
set_tests_properties(preferences-interaction PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

add_test(NAME preferences-dpi COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DpiAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-dpi-test)
set_tests_properties(preferences-dpi PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)

add_test(NAME preferences-performance COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreferencesPerformanceAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/preferences-performance-test)
set_tests_properties(preferences-performance PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

add_test(NAME live2d-pointer-motion COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Live2DPointerAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/live2d-pointer-test)
set_tests_properties(live2d-pointer-motion PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE
  SKIP_RETURN_CODE 77)

add_test(NAME windows-click-through COMMAND powershell.exe -NoProfile
  -ExecutionPolicy Bypass -File
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClickThroughAudit.ps1
  -Exe $<TARGET_FILE:bongo_cat>
  -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/click-through-test)
set_tests_properties(windows-click-through PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

set_tests_properties(preferences-navigation preferences-slider-drag
  preferences-model-border preferences-interaction preferences-dpi
  preferences-performance PROPERTIES SKIP_RETURN_CODE 77)
