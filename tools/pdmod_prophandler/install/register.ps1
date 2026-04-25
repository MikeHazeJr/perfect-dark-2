<#
register.ps1 -- Priority M / B-238 / M-2.2

Registers PD2ModPropHandler.dll as the Windows Shell property handler
for .pdmod files. Run as Administrator.

Per design Section 4.5.2 the registry shape is:

  HKCR\.pdmod
      (Default)                 = "PerfectDark2.Mod"
      PerceivedType             = "compressed"

  HKCR\PerfectDark2.Mod
      (Default)                 = "Perfect Dark 2 Mod"

  HKCR\PerfectDark2.Mod\shellex\PropertyHandler
      (Default)                 = "{<handler CLSID>}"

  HKCR\.pdmod\shellex\PropertyHandler
      (Default)                 = "{<handler CLSID>}"

  HKCR\CLSID\{<handler CLSID>}\InProcServer32
      (Default)                 = "<dll path>"
      ThreadingModel            = "Both"

  HKLM\Software\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.pdmod
      (Default)                 = "{<handler CLSID>}"

The CLSID is the same one the DLL exports
  (PD2ModPropHandler.cpp:CLSID_PD2ModPropHandler).

Usage:
  PowerShell -ExecutionPolicy Bypass -File register.ps1 -DllPath "C:\Path\To\PD2ModPropHandler.dll"

After running, restart Explorer (Task Manager -> "Restart" Windows Explorer)
to pick up the new property handler. Right-click any .pdmod and look at
the Properties -> Details tab to see Title / Author / Comment / Version.
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$DllPath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $DllPath)) {
    Write-Error "DLL not found: $DllPath"
    exit 1
}

$DllPath = (Resolve-Path $DllPath).Path
$Clsid   = "{6F4D7064-D2BB-4F38-8B95-7A2B9C4BD61F}"
$ProgId  = "PerfectDark2.Mod"

Write-Host "Registering PD2ModPropHandler:"
Write-Host "  DLL   : $DllPath"
Write-Host "  CLSID : $Clsid"
Write-Host "  ProgID: $ProgId"
Write-Host ""

# Helper: ensure a registry key exists.
function Ensure-Key($Path) {
    if (-not (Test-Path $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }
}

# 1. .pdmod -> ProgID + PerceivedType=compressed
Ensure-Key "Registry::HKEY_CLASSES_ROOT\.pdmod"
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\.pdmod" -Name "(default)" -Value $ProgId
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\.pdmod" -Name "PerceivedType" -Value "compressed"

# 2. ProgID friendly name
Ensure-Key "Registry::HKEY_CLASSES_ROOT\$ProgId"
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\$ProgId" -Name "(default)" -Value "Perfect Dark 2 Mod"

# 3. ProgID and extension shellex PropertyHandler bindings
Ensure-Key "Registry::HKEY_CLASSES_ROOT\$ProgId\shellex\PropertyHandler"
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\$ProgId\shellex\PropertyHandler" -Name "(default)" -Value $Clsid
Ensure-Key "Registry::HKEY_CLASSES_ROOT\.pdmod\shellex\PropertyHandler"
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\.pdmod\shellex\PropertyHandler" -Name "(default)" -Value $Clsid

# 4. CLSID -> InProcServer32 -> DLL path
Ensure-Key "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid\InProcServer32"
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid\InProcServer32" -Name "(default)" -Value $DllPath
Set-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid\InProcServer32" -Name "ThreadingModel" -Value "Both"

# 5. Property system binding
$psPath = "Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.pdmod"
Ensure-Key $psPath
Set-ItemProperty -Path $psPath -Name "(default)" -Value $Clsid

Write-Host "Registration complete."
Write-Host ""
Write-Host "To pick up the change in Explorer immediately, run:"
Write-Host "  taskkill /f /im explorer.exe; start explorer.exe"
Write-Host ""
Write-Host "Then right-click a .pdmod file and check the Properties -> Details tab."
