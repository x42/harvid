; The name of the installer
Name "harvid"

; The file to write
OutFile "harvid_installer-@VERSION@.exe"

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
  
  ; Put file there
  File "harvid.exe"
  File "harvid.nsi"
  File "avcodec-54.dll"
  File "avdevice-53.dll"
  File "avfilter-2.dll"
  File "avformat-54.dll"
  File "avresample-0.dll"
  File "avutil-51.dll"
  File "libjpeg-8.dll"
  File "postproc-52.dll"
  File "pthreadGC2.dll"
  File "swresample-0.dll"
  File "swscale-2.dll"
  File "zlib1.dll"
  File "cygwin1.dll"

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
  CreateShortCut "$SMPROGRAMS\harvid\harvid.lnk" "$INSTDIR\harvid.exe" "" "$INSTDIR\harvid.exe" 0
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
  Delete $INSTDIR\avcodec-54.dll
  Delete $INSTDIR\avdevice-53.dll
  Delete $INSTDIR\avfilter-2.dll
  Delete $INSTDIR\avformat-54.dll
  Delete $INSTDIR\avresample-0.dll
  Delete $INSTDIR\avutil-51.dll
  Delete $INSTDIR\libjpeg-8.dll
  Delete $INSTDIR\postproc-52.dll
  Delete $INSTDIR\pthreadGC2.dll
  Delete $INSTDIR\swresample-0.dll
  Delete $INSTDIR\swscale-2.dll
  Delete $INSTDIR\zlib1.dll
  Delete $INSTDIR\cygwin1.dll
  Delete $INSTDIR\uninstall.exe

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\harvid\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\harvid"
  RMDir "$INSTDIR"

SectionEnd
