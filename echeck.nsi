; echeck.nsi
;--------------------------------

; The name of the installer
Name "ECheck"

; The file to write
OutFile "..\echeckinst.exe"

; Request application privileges for Windows Vista and higher
RequestExecutionLevel admin

; Build Unicode installer
Unicode True

; The default installation directory
InstallDir $PROGRAMFILES\Eressea\ECheck

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\ECheck" "Install_Dir"

;--------------------------------

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "ECheck (required)"

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put files there
  File out\build\x86-Release\echeck.exe
  File out\build\x86-Release\echeckw.exe
  File out\build\x86-Release\*.dll
  SetOutPath $INSTDIR\locale
  File /r locale\*.*
  SetOutPath $INSTDIR\e2
  File /r e2\*.*
  SetOutPath $INSTDIR\e3
  File /r e3\*.*
  
  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\ECheck "Install_Dir" "$INSTDIR"
  WriteRegStr HKCU SOFTWARE\Eressea\CSMap\settings "echeck_dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ECheck" "DisplayName" "ECheck"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ECheck" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ECheck" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ECheck" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
  
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ECheck"
  DeleteRegKey HKLM SOFTWARE\ECheck

  ; Remove files and uninstaller
  RMDir /r $INSTDIR\locale
  RMDir /r $INSTDIR\e2
  RMDir /r $INSTDIR\e3
  Delete $INSTDIR\echeck.exe
  Delete $INSTDIR\echeckw.exe
  Delete $INSTDIR\*.dll
  Delete $INSTDIR\uninstall.exe

  RMDir "$INSTDIR"

SectionEnd
