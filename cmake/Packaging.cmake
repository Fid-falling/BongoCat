install(TARGETS bongo_cat
  RUNTIME DESTINATION . COMPONENT Runtime
  BUNDLE DESTINATION . COMPONENT Runtime)
if(UNIX AND NOT APPLE)
  install(DIRECTORY resources/assets DESTINATION . COMPONENT Runtime
    PATTERN "logo-mac.png" EXCLUDE PATTERN "ui-icons.png" EXCLUDE
    PATTERN "ui-symbols-1x.png" EXCLUDE PATTERN "ui-symbols-4x.png" EXCLUDE)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(BONGO_CAT_PACKAGE_ARCH "x64")
else()
  set(BONGO_CAT_PACKAGE_ARCH "x86")
endif()

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES BONGO_CAT_OSX_ARCH_COUNT)
    if(BONGO_CAT_OSX_ARCH_COUNT GREATER 1)
      set(BONGO_CAT_PACKAGE_ARCH "universal")
    else()
      list(GET CMAKE_OSX_ARCHITECTURES 0 BONGO_CAT_OSX_ARCH)
      if(BONGO_CAT_OSX_ARCH MATCHES "^(arm64|aarch64)$")
        set(BONGO_CAT_PACKAGE_ARCH "arm64")
      endif()
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(BONGO_CAT_PACKAGE_ARCH "arm64")
  endif()
  set(BONGO_CAT_PACKAGE_PLATFORM "macos-${BONGO_CAT_PACKAGE_ARCH}")
  set(CPACK_GENERATOR "ZIP")
elseif(WIN32)
  set(BONGO_CAT_PACKAGE_PLATFORM "windows-${BONGO_CAT_PACKAGE_ARCH}")
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "BongoCat")
  set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "BongoCat")
  set(CPACK_MONOLITHIC_INSTALL ON)
  set(CPACK_COMPONENT_INSTALL OFF)
  set(CPACK_PACKAGE_EXECUTABLES "BongoCat" "BongoCat")
  set(CPACK_CREATE_DESKTOP_LINKS "BongoCat")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA/Programs")
  set(CPACK_NSIS_DISPLAY_NAME "BongoCat")
  set(CPACK_NSIS_PACKAGE_NAME "BongoCat")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "BongoCat.exe")
  set(CPACK_NSIS_MUI_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico")
  set(CPACK_NSIS_MUI_UNIICON
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "BongoCat.exe")
  set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
else()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(BONGO_CAT_PACKAGE_ARCH "arm64")
  endif()
  set(BONGO_CAT_PACKAGE_PLATFORM "linux-${BONGO_CAT_PACKAGE_ARCH}")
  set(CPACK_GENERATOR "TGZ")
endif()

set(BONGO_CAT_PACKAGE_NAME
  "BongoCat-${PROJECT_VERSION}-${BONGO_CAT_PACKAGE_PLATFORM}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/bongo-cat-package-name.txt"
  CONTENT "${BONGO_CAT_PACKAGE_NAME}\n")

set(CPACK_PACKAGE_NAME "BongoCat")
set(CPACK_PACKAGE_VENDOR "BongoCat")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${BONGO_CAT_PACKAGE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALL_CMAKE_PROJECTS
  "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")

if(WIN32)
  # CPack's stock NSIS template requests elevation and switches administrator
  # accounts to the machine-wide shell context. Generate a small, versioned
  # override from the CMake-provided template so this installer is always
  # current-user-only, including when the logged-in user belongs to
  # Administrators.
  function(bongo_cat_replace_nsis_function input_variable function_name
      replacement output_variable)
    set(source "${${input_variable}}")
    string(FIND "${source}" "Function ${function_name}" start)
    if(start LESS 0)
      message(FATAL_ERROR
        "Could not customize the CPack NSIS ${function_name} function")
    endif()
    string(SUBSTRING "${source}" ${start} -1 function_tail)
    string(FIND "${function_tail}" "FunctionEnd" relative_finish)
    if(relative_finish LESS 0)
      message(FATAL_ERROR
        "Could not find the end of the CPack NSIS ${function_name} function")
    endif()
    math(EXPR finish "${start} + ${relative_finish}")
    math(EXPR suffix_start "${finish} + 11")
    string(SUBSTRING "${source}" 0 ${start} prefix)
    string(SUBSTRING "${source}" ${suffix_start} -1 suffix)
    set(${output_variable} "${prefix}${replacement}${suffix}" PARENT_SCOPE)
  endfunction()

  set(BONGO_CAT_CPACK_TEMPLATE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/cpack-templates")
  file(MAKE_DIRECTORY "${BONGO_CAT_CPACK_TEMPLATE_DIR}")
  file(READ "${CMAKE_ROOT}/Modules/Internal/CPack/NSIS.template.in"
    BONGO_CAT_NSIS_TEMPLATE)
  string(REPLACE "\r\n" "\n" BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[RequestExecutionLevel admin]=]
    [=[RequestExecutionLevel user]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  !include "MUI.nsh"]=]
    [=[  !include "MUI.nsh"
  !include "WordFunc.nsh"
  !include "Win\RestartManager.nsh"]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  Var IS_DEFAULT_INSTALLDIR]=]
    [=[  Var IS_DEFAULT_INSTALLDIR
  Var BONGO_CAT_UPGRADE_DIR
  Var BONGO_CAT_UPDATE_SHUTDOWN]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  !insertmacro MUI_PAGE_DIRECTORY]=]
    [=[  !define MUI_PAGE_CUSTOMFUNCTION_PRE bongo_cat_directory_page_pre
  !insertmacro MUI_PAGE_DIRECTORY]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[Function InstallOptionsPage]=]
    [=[Function bongo_cat_directory_page_pre
  StrCmp "$BONGO_CAT_UPGRADE_DIR" "" bongo_cat_directory_page_show
  Abort
bongo_cat_directory_page_show:
FunctionEnd

Function InstallOptionsPage]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_CREATE_ICONS_EXTRA@]=] [=[
  SetOutPath "$SMPROGRAMS\$STARTMENU_FOLDER"
  CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\BongoCat.lnk" \
    "$INSTDIR\.\BongoCat.exe" \
    "--nearby-root=$\"$SMPROGRAMS\$STARTMENU_FOLDER$\""
  StrCmp "$INSTALL_DESKTOP" "1" bongo_cat_create_desktop_shortcut \
    bongo_cat_shortcuts_done
bongo_cat_create_desktop_shortcut:
  SetOutPath "$DESKTOP"
  CreateShortCut "$DESKTOP\BongoCat.lnk" "$INSTDIR\.\BongoCat.exe" \
    "--nearby-root=$\"$DESKTOP$\""
bongo_cat_shortcuts_done:
  SetOutPath "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_EXTRA_INSTALL_COMMANDS@]=] [=[
  WriteRegStr SHCTX \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BongoCat" \
    "InstallLocation" "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS@]=] [=[
  StrCmp "$BONGO_CAT_UPGRADE_DIR" "" bongo_cat_upgrade_ready
  StrCmp "$BONGO_CAT_UPDATE_SHUTDOWN" "1" \
    bongo_cat_signal_update_shutdown bongo_cat_find_legacy_tray

bongo_cat_signal_update_shutdown:
  ClearErrors
  ExecWait '"$BONGO_CAT_UPGRADE_DIR\BongoCat.exe" --shutdown-for-update' $3
  IfErrors bongo_cat_restart_manager_shutdown
  StrCmp "$3" "0" bongo_cat_uninstall_old \
    bongo_cat_restart_manager_shutdown

  ; Versions before update shutdown support interpret WM_CLOSE as "hide to
  ; tray". Find a legacy SDL tray window owned by the exact installed
  ; executable and trigger its Exit entry so state is flushed normally.
bongo_cat_find_legacy_tray:
  StrCpy $0 0
  System::Call 'user32::FindWindowExW(p -3, p r0, w "Message", p 0) p .r0'
  StrCmp "$0" "0" bongo_cat_restart_manager_shutdown
  System::Call 'user32::GetWindowThreadProcessId(p r0, *i .r6)'
  System::Call 'kernel32::OpenProcess(i 0x00101000, i 0, i r6) p .r8'
  StrCmp "$8" "0" bongo_cat_find_legacy_tray
  StrCpy $5 1024
  System::Call 'kernel32::QueryFullProcessImageNameW(p r8, i 0, \
    w .r7, *i r5) i .r9'
  StrCmp "$9" "0" bongo_cat_close_unmatched_process
  StrCmp "$7" "$BONGO_CAT_UPGRADE_DIR\BongoCat.exe" 0 \
    bongo_cat_close_unmatched_process
  ; Each legacy tray creation consumes eight SDL command identifiers. Try
  ; the Exit slot for several shell-restoration generations.
  StrCpy $3 8
bongo_cat_signal_legacy_tray_exit:
  System::Call 'user32::PostMessageW(p r0, i 0x0111, p r3, p 0)'
  IntOp $3 $3 + 8
  IntCmp $3 136 bongo_cat_wait_for_legacy_shutdown \
    bongo_cat_signal_legacy_tray_exit bongo_cat_wait_for_legacy_shutdown

bongo_cat_wait_for_legacy_shutdown:
  System::Call 'kernel32::WaitForSingleObject(p r8, i 10000) i .r9'
  System::Call 'kernel32::CloseHandle(p r8)'
  StrCmp "$9" "0" bongo_cat_uninstall_old \
    bongo_cat_restart_manager_shutdown

bongo_cat_close_unmatched_process:
  System::Call 'kernel32::CloseHandle(p r8)'
  Goto bongo_cat_find_legacy_tray

bongo_cat_restart_manager_shutdown:
  ; Tray-disabled legacy versions have no graceful remote-exit API. Windows
  ; Restart Manager shuts down only processes using this exact executable.
  !insertmacro RestartManager_ShutdownFile \
    "$BONGO_CAT_UPGRADE_DIR\BongoCat.exe" $9
  StrCmp "$9" "0" bongo_cat_uninstall_old bongo_cat_upgrade_failed

bongo_cat_uninstall_old:
  IfFileExists "$BONGO_CAT_UPGRADE_DIR\Uninstall.exe" 0 \
    bongo_cat_upgrade_ready
  ClearErrors
  ExecWait '"$BONGO_CAT_UPGRADE_DIR\Uninstall.exe" /S \
    _?=$BONGO_CAT_UPGRADE_DIR' $3
  IfErrors bongo_cat_upgrade_failed
  StrCmp "$3" "0" bongo_cat_upgrade_ready bongo_cat_upgrade_failed

bongo_cat_upgrade_failed:
  MessageBox MB_OK|MB_ICONSTOP \
    "BongoCat could not be updated. Close the running application and try again."
  Abort

bongo_cat_upgrade_ready:
  SetOutPath "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  bongo_cat_replace_nsis_function(BONGO_CAT_NSIS_TEMPLATE ".onInit"
    [=[Function .onInit
  SetShellVarContext current
  StrCpy $BONGO_CAT_UPGRADE_DIR ""
  StrCpy $BONGO_CAT_UPDATE_SHUTDOWN ""

  ReadRegStr $0 SHCTX \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BongoCat" \
    "DisplayVersion"
  StrCmp "$0" "" bongo_cat_check_legacy
  ReadRegStr $1 SHCTX \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BongoCat" \
    "InstallLocation"
  StrCmp "$1" "" 0 bongo_cat_compare_versions
  ReadRegStr $1 SHCTX "Software\BongoCat\BongoCat" ""
bongo_cat_compare_versions:
  StrCmp "$1" "" bongo_cat_install
  IfFileExists "$1\BongoCat.exe" 0 bongo_cat_install
  ${VersionCompare} "$0" "@CPACK_PACKAGE_VERSION@" $2
  StrCmp "$2" "2" bongo_cat_upgrade
  Goto bongo_cat_launch_installed

bongo_cat_check_legacy:
  GetFullPathName $1 "$LOCALAPPDATA\..\LocalPrograms\BongoCat"
  IfFileExists "$1\BongoCat.exe" 0 bongo_cat_install
  ClearErrors
  GetDLLVersion "$1\BongoCat.exe" $6 $7
  IfErrors bongo_cat_install
  IntOp $0 $6 >> 16
  IntOp $8 $6 & 0x0000FFFF
  IntOp $9 $7 >> 16
  StrCpy $0 "$0.$8.$9"
  ${VersionCompare} "$0" "@CPACK_PACKAGE_VERSION@" $2
  StrCmp "$2" "2" bongo_cat_upgrade_legacy

bongo_cat_launch_installed:
  ClearErrors
  Exec '"$1\BongoCat.exe"'
  IfErrors bongo_cat_launch_failed
  Quit

bongo_cat_launch_failed:
  MessageBox MB_OK|MB_ICONSTOP \
    "The installed BongoCat could not be started."
  Quit

bongo_cat_upgrade:
  ; 0.1.0 treats an unknown update argument as a normal launch. Waiting for
  ; that process would block forever when the application was not running.
  ${VersionCompare} "$0" "0.1.1" $2
  StrCmp "$2" "2" bongo_cat_upgrade_paths
  StrCpy $BONGO_CAT_UPDATE_SHUTDOWN "1"
bongo_cat_upgrade_paths:
  StrCpy $BONGO_CAT_UPGRADE_DIR "$1"
  StrCpy $INSTDIR "$1"
  Goto bongo_cat_install

bongo_cat_upgrade_legacy:
  StrCpy $BONGO_CAT_UPGRADE_DIR "$1"
  StrCpy $INSTDIR "$1"
  Goto bongo_cat_install

bongo_cat_install:
  SetShellVarContext current
FunctionEnd]=]
    BONGO_CAT_NSIS_TEMPLATE)
  bongo_cat_replace_nsis_function(BONGO_CAT_NSIS_TEMPLATE "un.onInit"
    [=[Function un.onInit
  SetShellVarContext current
FunctionEnd]=]
    BONGO_CAT_NSIS_TEMPLATE)
  file(WRITE "${BONGO_CAT_CPACK_TEMPLATE_DIR}/NSIS.template.in"
    "${BONGO_CAT_NSIS_TEMPLATE}")
  list(PREPEND CMAKE_MODULE_PATH "${BONGO_CAT_CPACK_TEMPLATE_DIR}")
endif()

include(CPack)

if(WIN32)
  add_custom_target(package-installer
    COMMAND "${CMAKE_CPACK_COMMAND}" -G NSIS -C "$<CONFIG>"
      --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
      -D "CPACK_PACKAGE_FILE_NAME=${BONGO_CAT_PACKAGE_NAME}-setup"
      -D "CPACK_INCLUDE_TOPLEVEL_DIRECTORY=OFF"
    DEPENDS bongo_cat
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building the BongoCat Windows installer"
    VERBATIM)
endif()
