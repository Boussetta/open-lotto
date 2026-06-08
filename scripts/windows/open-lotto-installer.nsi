; NSIS Installer for Open Lotto
; This script creates an installer for open-lotto on Windows
; Install NSIS from https://nsis.sourceforge.io/Download

!include "MUI2.nsh"
!include "x64.nsh"

; Name and file
Name "Open Lotto"
OutFile "open-lotto-x64-setup.exe"
InstallDir "$PROGRAMFILES\OpenLotto"

; Request admin privileges
RequestExecutionLevel admin

; MUI Settings
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; Installer sections
Section "Install"
  SetOutPath "$INSTDIR"
  
  ; Copy executable
  File "..\\..\\open-lotto.exe"
  
  ; Copy all bundled runtime DLLs collected by CI packaging step
  File "..\\..\\*.dll"

  ; Copy lottery plugins
  SetOutPath "$INSTDIR\\plugins"
  File "..\\..\\build\\plugins\\*.dll"
  SetOutPath "$INSTDIR"
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  ; Create Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\OpenLotto"
  CreateShortcut "$SMPROGRAMS\OpenLotto\Open Lotto.lnk" "$INSTDIR\open-lotto.exe"
  CreateShortcut "$SMPROGRAMS\OpenLotto\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  
  ; Create Desktop shortcut
  CreateShortcut "$DESKTOP\Open Lotto.lnk" "$INSTDIR\open-lotto.exe"
  
  ; Write install info to registry
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLotto" \
    "DisplayName" "Open Lotto - Modular Lottery Generator"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLotto" \
    "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLotto" \
    "InstallLocation" "$INSTDIR"
  
  ; Associate .lotto files with the application (optional)
  WriteRegStr HKCR ".lotto" "" "OpenLottoConfig"
  WriteRegStr HKCR "OpenLottoConfig" "" "Open Lotto Configuration File"
  WriteRegStr HKCR "OpenLottoConfig\shell\open\command" "" "$INSTDIR\open-lotto.exe %1"
  
SectionEnd

; Uninstaller section
Section "Uninstall"
  ; Remove executable and bundled runtime DLLs
  Delete "$INSTDIR\open-lotto.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\plugins\*.dll"
  RMDir "$INSTDIR\plugins"
  Delete "$INSTDIR\Uninstall.exe"
  
  ; Remove directory
  RMDir "$INSTDIR"
  
  ; Remove Start Menu shortcuts
  Delete "$SMPROGRAMS\OpenLotto\Open Lotto.lnk"
  Delete "$SMPROGRAMS\OpenLotto\Uninstall.lnk"
  RMDir "$SMPROGRAMS\OpenLotto"
  
  ; Remove Desktop shortcut
  Delete "$DESKTOP\Open Lotto.lnk"
  
  ; Remove registry entries
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenLotto"
  DeleteRegKey HKCR ".lotto"
  DeleteRegKey HKCR "OpenLottoConfig"
  
SectionEnd

; Version information
VIProductVersion "0.7.0.0"
VIAddVersionKey "ProductName" "Open Lotto"
VIAddVersionKey "ProductVersion" "0.7.0"
VIAddVersionKey "CompanyName" "Boussetta"
VIAddVersionKey "LegalCopyright" "2025 - MIT License"
VIAddVersionKey "FileDescription" "Open Lotto Setup Wizard"
