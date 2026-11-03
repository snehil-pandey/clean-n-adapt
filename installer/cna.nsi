; Clean-n-Adapt installer (NSIS 3.x, Modern UI 2).
;
; Per-user install: no administrator rights, no UAC prompt. Settings, knowledge base and quarantined
; files live in %LOCALAPPDATA%\cna and are NEVER removed silently by an upgrade or an uninstall.
;
; Build (done for you by CMake or scripts/build-mingw.sh):
;   makensis -DVERSION=0.1.0 -DCNA_EXE=<cna.exe> -DWIDGET_EXE=<cna-widget.exe> \
;            -DROOT=<source root> -DOUT_DIR=<output dir> installer/cna.nsi
; Silent install:   Clean-n-Adapt-Setup-<ver>.exe /S            (optionally /D=C:\Some\Folder, must be last)
; Silent uninstall: "%LOCALAPPDATA%\Programs\Clean-n-Adapt\Uninstall.exe" /S

Unicode true
!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef CNA_EXE
  !error "pass -DCNA_EXE=<path to cna.exe>"
!endif
!ifndef WIDGET_EXE
  !error "pass -DWIDGET_EXE=<path to cna-widget.exe>"
!endif
!ifndef ROOT
  !define ROOT ".."
!endif
!ifndef OUT_DIR
  !define OUT_DIR "."
!endif

!define APP        "Clean-n-Adapt"
!define PUBLISHER  "Clean-n-Adapt"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CleanNAdapt"
!define RUN_KEY    "Software\Microsoft\Windows\CurrentVersion\Run"
!define TASK_NAME  "cna-auto"

Name "${APP} ${VERSION}"
OutFile "${OUT_DIR}/Clean-n-Adapt-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APP} ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "Copyright (C) ${PUBLISHER}"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define MUI_ICON   "${ROOT}\assets\cna.ico"
!define MUI_UNICON "${ROOT}\assets\cna.ico"
!define MUI_ABORTWARNING
; The branded sidebar is optional so source-only checkouts and release builds
; remain packageable even when artwork is intentionally omitted.
!if /FileExists "${ROOT}\assets\installer-side.bmp"
  !define MUI_WELCOMEFINISHPAGE_BITMAP   "${ROOT}\assets\installer-side.bmp"
  !define MUI_UNWELCOMEFINISHPAGE_BITMAP "${ROOT}\assets\installer-side.bmp"
!endif
!define MUI_FINISHPAGE_RUN "$INSTDIR\cna-widget.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP} now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------------------------------
Section "${APP} (required)" SecMain
  SectionIn RO

  ; The widget only hides when closed, so ask a running copy to exit before replacing its files.
  IfFileExists "$INSTDIR\cna-widget.exe" 0 +3
    ExecWait '"$INSTDIR\cna-widget.exe" --quit'
    Sleep 1200

  SetOutPath "$INSTDIR"
  File "${CNA_EXE}"
  File "${WIDGET_EXE}"
  File "${ROOT}\README.md"
  File "${ROOT}\CHANGELOG.md"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start menu
  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\cna-widget.exe" "" "$INSTDIR\cna-widget.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP}\${APP} command line.lnk" "$SYSDIR\cmd.exe" "/k cna.exe help" "$INSTDIR\cna.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk" "$INSTDIR\Uninstall.exe"

  ; Apps & features entry
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayName"     "${APP}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINST_KEY}" "Publisher"       "${PUBLISHER}"
  WriteRegStr   HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\cna-widget.exe"
  WriteRegStr   HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "Documentation" SecDocs
  SetOutPath "$INSTDIR\docs"
  File /r "${ROOT}\docs\*.*"
  SetOutPath "$INSTDIR"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP} documentation.lnk" "$INSTDIR\docs"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  SetOutPath "$INSTDIR"
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\cna-widget.exe" "" "$INSTDIR\cna-widget.exe" 0
SectionEnd

Section /o "Start with Windows" SecAutostart
  WriteRegStr HKCU "${RUN_KEY}" "cna-widget" '"$INSTDIR\cna-widget.exe"'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}      "The command-line engine (cna.exe) and the desktop/tray widget (cna-widget.exe)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDocs}      "README, architecture, command reference and safety notes."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}   "Adds a shortcut to the widget on your desktop."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutostart} "Starts the widget (tray icon) automatically when you sign in to Windows."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ------------------------------------------------------------------------------------------------
Section "Uninstall"
  IfFileExists "$INSTDIR\cna-widget.exe" 0 +3
    ExecWait '"$INSTDIR\cna-widget.exe" --quit'
    Sleep 1200

  ; things the program itself may have created
  nsExec::Exec 'schtasks /Delete /TN "${TASK_NAME}" /F'
  Pop $0
  DeleteRegValue HKCU "${RUN_KEY}" "cna-widget"

  Delete "$INSTDIR\cna.exe"
  Delete "$INSTDIR\cna-widget.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\CHANGELOG.md"
  RMDir /r "$INSTDIR\docs"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"   ; only succeeds when empty, so a portable ".state" folder beside the exe is left alone

  Delete "$SMPROGRAMS\${APP}\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\${APP} command line.lnk"
  Delete "$SMPROGRAMS\${APP}\${APP} documentation.lnk"
  Delete "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk"
  RMDir  "$SMPROGRAMS\${APP}"
  Delete "$DESKTOP\${APP}.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"

  ; User data is kept unless the user explicitly says otherwise (a silent uninstall always keeps it).
  IfFileExists "$LOCALAPPDATA\cna\*.*" 0 done
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 \
    "Also delete your settings, knowledge base and QUARANTINED files in:$\r$\n$LOCALAPPDATA\cna$\r$\n$\r$\nQuarantined items cannot be restored afterwards. Choose No to keep them." \
    /SD IDNO IDNO done
  RMDir /r "$LOCALAPPDATA\cna"
  done:
SectionEnd
