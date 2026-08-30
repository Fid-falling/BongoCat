bongo_cat_replace_nsis_function(BONGO_CAT_NSIS_TEMPLATE ".onInit"
  [=[Function .onInit
  SetShellVarContext current
  StrCpy $STARTMENU_FOLDER "BongoCat"
  StrCpy $INSTALL_DESKTOP "1"
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
  StrCpy $BONGO_CAT_KEEP_USER_DATA "1"
FunctionEnd]=]
  BONGO_CAT_NSIS_TEMPLATE)
file(WRITE "${BONGO_CAT_CPACK_TEMPLATE_DIR}/NSIS.template.in"
  "${BONGO_CAT_NSIS_TEMPLATE}")
list(PREPEND CMAKE_MODULE_PATH "${BONGO_CAT_CPACK_TEMPLATE_DIR}")
