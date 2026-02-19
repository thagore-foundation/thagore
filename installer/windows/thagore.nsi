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

Section "Install LLVM 21.1.8" SecLLVM
  MessageBox MB_YESNO "Do you want to install LLVM 21.1.8 automatically?" IDYES llvm_begin IDNO llvm_done
llvm_begin:
  DetailPrint "Checking clang..."
  nsExec::ExecToLog 'cmd /C clang --version'
  Pop $0
  ${If} $0 == 0
    DetailPrint "LLVM already available."
    Goto llvm_done
  ${EndIf}

  DetailPrint "Installing LLVM 21.1.8 via winget..."
  nsExec::ExecToLog 'cmd /C winget --version'
  Pop $1
  ${If} $1 != 0
    MessageBox MB_ICONSTOP "winget not found. Please install LLVM 21.1.8 manually."
    Abort
  ${EndIf}
  nsExec::ExecToLog 'cmd /C winget install --id LLVM.LLVM --version 21.1.8 --silent --accept-package-agreements --accept-source-agreements'
  Pop $2
  ${If} $2 != 0
    MessageBox MB_ICONSTOP "LLVM auto-install failed. Please install LLVM 21.1.8 manually."
    Abort
  ${EndIf}

llvm_done:
SectionEnd

Section "Add to PATH" SecPath
  ; Update Machine + User PATH idempotently (no duplicate entries).
  nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -Command "$target = ''$INSTDIR\bin''; $scopes = @(''Machine'',''User''); foreach ($scope in $scopes) { $cur = [Environment]::GetEnvironmentVariable(''Path'', $scope); if ([string]::IsNullOrEmpty($cur)) { [Environment]::SetEnvironmentVariable(''Path'', $target, $scope); continue }; $parts = $cur -split '';''; if (-not ($parts -contains $target)) { [Environment]::SetEnvironmentVariable(''Path'', ($cur.TrimEnd('';'') + '';'' + $target), $scope) } }"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION "PATH update returned code $0. You may need to add $INSTDIR\bin manually."
  ${EndIf}
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment"
SectionEnd

Section "Uninstall"
  nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -Command "$target = ''$INSTDIR\bin''; $scopes = @(''Machine'',''User''); foreach ($scope in $scopes) { $cur = [Environment]::GetEnvironmentVariable(''Path'', $scope); if ([string]::IsNullOrEmpty($cur)) { continue }; $parts = @(); foreach ($p in ($cur -split '';'')) { if ($p -and ($p -ne $target)) { $parts += $p } }; [Environment]::SetEnvironmentVariable(''Path'', ($parts -join '';''), $scope) }"'
  Delete "$INSTDIR\bin\thagore.exe"
  Delete "$INSTDIR\bin\thag.exe"
  RMDir /r "$INSTDIR\lib\std"
  RMDir "$INSTDIR\bin"
  RMDir "$INSTDIR\lib"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Thagore"
SectionEnd
