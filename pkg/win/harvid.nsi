; The name of the installer
Name "harvid"

; The file to write
OutFile "harvid_installer-@WARCH@-@VERSION@.exe"

; The default installation directory
InstallDir $PROGRAMFILES\harvid

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\RSS\harvid" "Install_Dir"

;--------------------------------

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "harvid (required)"

  SectionIn RO

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  File "harvid.exe"
  File "harvid.nsi"
  File "ffmpeg.exe"
  File "ffprobe.exe"

  FILE /r "*.dll"

  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\RSS\harvid "Install_Dir" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid" "DisplayName" "harvid"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"
  CreateDirectory "$SMPROGRAMS\harvid"
  CreateShortCut "$SMPROGRAMS\harvid\harvid.lnk" "$INSTDIR\harvid.exe" "-A shutdown" "$INSTDIR\harvid.exe" 0
  CreateShortCut "$SMPROGRAMS\harvid\uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\harvid"
  DeleteRegKey HKLM SOFTWARE\RSS\harvid

  ; Remove files and uninstaller
  Delete $INSTDIR\harvid.exe
  Delete $INSTDIR\harvid.nsi
  Delete $INSTDIR\uninstall.exe
  Delete $INSTDIR\ffmpeg.exe
  Delete $INSTDIR\ffprobe.exe

  Delete "$INSTDIR\*.dll"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\harvid\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\harvid"
  RMDir "$INSTDIR"

SectionEnd
