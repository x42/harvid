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

  File "harvid.exe"
  File "harvid.nsi"
  File "ffmpeg.exe"
  File "ffprobe.exe"

  FILE "avcodec-55.dll"
  FILE "avdevice-55.dll"
  FILE "avfilter-4.dll"
  FILE "avformat-55.dll"
  FILE "avutil-52.dll"
  FILE "libcharset-1.dll"
  FILE "libiconv-2.dll"
  FILE "libjpeg-9.dll"
  FILE "libmp3lame-0.dll"
  FILE "libogg-0.dll"
  FILE "libpng16-16.dll"
  FILE "libtheora-0.dll"
  FILE "libtheoradec-1.dll"
  FILE "libtheoraenc-1.dll"
  FILE "libvorbis-0.dll"
  FILE "libvorbisenc-2.dll"
  FILE "libvorbisfile-3.dll"
  FILE "libx264-142.dll"
  FILE "postproc-52.dll"
  FILE "pthreadGC2.dll"
  FILE "swresample-0.dll"
  FILE "swscale-2.dll"
  FILE "zlib1.dll"

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
  Delete $INSTDIR\avcodec-55.dll
  Delete $INSTDIR\avdevice-55.dll
  Delete $INSTDIR\avfilter-4.dll
  Delete $INSTDIR\avformat-55.dll
  Delete $INSTDIR\avutil-52.dll
  Delete $INSTDIR\libcharset-1.dll
  Delete $INSTDIR\libiconv-2.dll
  Delete $INSTDIR\libjpeg-9.dll
  Delete $INSTDIR\libmp3lame-0.dll
  Delete $INSTDIR\libogg-0.dll
  Delete $INSTDIR\libpng16-16.dll
  Delete $INSTDIR\libtheora-0.dll
  Delete $INSTDIR\libtheoradec-1.dll
  Delete $INSTDIR\libtheoraenc-1.dll
  Delete $INSTDIR\libvorbis-0.dll
  Delete $INSTDIR\libvorbisenc-2.dll
  Delete $INSTDIR\libvorbisfile-3.dll
  Delete $INSTDIR\libx264-142.dll
  Delete $INSTDIR\postproc-52.dll
  Delete $INSTDIR\pthreadGC2.dll
  Delete $INSTDIR\swresample-0.dll
  Delete $INSTDIR\swscale-2.dll
  Delete $INSTDIR\zlib1.dll

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\harvid\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\harvid"
  RMDir "$INSTDIR"

SectionEnd
