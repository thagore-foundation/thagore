; Thagore Windows UI Installer (NSIS)

!include "MUI2.nsh"
!include "WinMessages.nsh"

!ifndef VERSION
!define VERSION "0.0.0"
!endif

Name "Thagore Compiler ${VERSION}"
OutFile "thagore-windows-setup.exe"
InstallDir "$PROGRAMFILES64\Thagore"
InstallDirRegKey HKLM "Software\Thagore" "InstallDir"
RequestExecutionLevel admin

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Thagore Compiler" SecMain
  SetOutPath "$INSTDIR\bin"
  File "..\..\dist\bin\thagore.exe"
  File "..\..\dist\bin\thag.exe"

  SetOutPath "$INSTDIR\lib\std"
  File /r "..\..\dist\lib\std\*.*"

  WriteRegStr HKLM "Software\Thagore" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Add to PATH" SecPath
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  ${If} $0 == ""
    StrCpy $0 "$INSTDIR\bin"
  ${Else}
    StrCpy $0 "$0;$INSTDIR\bin"
  ${EndIf}
  WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
  System::Call 'Kernel32::SetEnvironmentVariable(t, t) i("Path", "$0").r1'
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\bin\thagore.exe"
  Delete "$INSTDIR\bin\thag.exe"
  RMDir /r "$INSTDIR\lib\std"
  RMDir "$INSTDIR\bin"
  RMDir "$INSTDIR\lib"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Thagore"
SectionEnd
