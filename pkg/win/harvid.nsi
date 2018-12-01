SetCompressor /SOLID lzma
SetCompressorDictSize 32
RequestExecutionLevel admin

Name "harvid"
OutFile "harvid_installer-@WARCH@-@VERSION@.exe"

InstallDir "$@PROGRAMFILES@\harvid"
InstallDirRegKey HKLM "Software\RSS\harvid\@WARCH@" "Install_Dir"

;--------------------------------

!include MUI2.nsh

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

;--------------------------------

Section "harvid (required)" SecMainProg
  SectionIn RO

  SetOutPath "$INSTDIR"

  File "harvid.exe"
  File "harvid.nsi"
  File "ffmpeg.exe"
  File "ffprobe.exe"

  FILE /r "*.dll"

  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\RSS\harvid\@WARCH@ "Install_Dir" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid-@WARCH@" "DisplayName" "harvid@SFX@"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid-@WARCH@" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid-@WARCH@" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid-@WARCH@" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Start Menu Shortcuts" SecMenu
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\harvid@SFX@"
  CreateShortCut "$SMPROGRAMS\harvid@SFX@\harvid.lnk" "$INSTDIR\harvid.exe" "-A shutdown" "$INSTDIR\harvid.exe" 0
  CreateShortCut "$SMPROGRAMS\harvid@SFX@\uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
SectionEnd

;--------------------------------

LangString DESC_SecMainProg ${LANG_ENGLISH} "Ardour video-tools; HTTP Ardour Video Daemon + ffmpeg, ffprobe"
LangString DESC_SecMenu ${LANG_ENGLISH} "Create Start-Menu Shortcuts (recommended)."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
!insertmacro MUI_DESCRIPTION_TEXT ${SecMainProg} $(DESC_SecMainProg)
!insertmacro MUI_DESCRIPTION_TEXT ${SecMenu} $(DESC_SecMenu)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------

; Uninstaller

Section "Uninstall"
  SetShellVarContext all

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid-@WARCH@"
  DeleteRegKey HKLM SOFTWARE\RSS\harvid
  DeleteRegKey HKLM SOFTWARE\RSS\harvid\@WARCH@

  ; Remove files and uninstaller
  Delete $INSTDIR\harvid.exe
  Delete $INSTDIR\harvid.nsi
  Delete $INSTDIR\uninstall.exe
  Delete $INSTDIR\ffmpeg.exe
  Delete $INSTDIR\ffprobe.exe

  Delete "$INSTDIR\*.dll"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\harvid@SFX@\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\harvid@SFX@"
  RMDir "$INSTDIR"

SectionEnd
