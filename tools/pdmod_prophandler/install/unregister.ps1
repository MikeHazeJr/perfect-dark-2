<#
unregister.ps1 -- Priority M / B-238 / M-2.2

Removes the registry entries created by register.ps1. Run as
Administrator. Does NOT delete the DLL itself; the operator handles that
separately if desired.

Usage:
  PowerShell -ExecutionPolicy Bypass -File unregister.ps1
#>

$ErrorActionPreference = "Continue"

$Clsid  = "{6F4D7064-D2BB-4F38-8B95-7A2B9C4BD61F}"
$ProgId = "PerfectDark2.Mod"

function Remove-KeyIfPresent($Path) {
    if (Test-Path $Path) {
        Remove-Item -Path $Path -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  removed: $Path"
    } else {
        Write-Host "  absent : $Path"
    }
}

Write-Host "Unregistering PD2ModPropHandler:"
Remove-KeyIfPresent "Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.pdmod"
Remove-KeyIfPresent "Registry::HKEY_CLASSES_ROOT\.pdmod\shellex\PropertyHandler"
Remove-KeyIfPresent "Registry::HKEY_CLASSES_ROOT\$ProgId\shellex\PropertyHandler"
Remove-KeyIfPresent "Registry::HKEY_CLASSES_ROOT\$ProgId\shellex"
Remove-KeyIfPresent "Registry::HKEY_CLASSES_ROOT\$ProgId"
Remove-KeyIfPresent "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid"

# We intentionally do NOT remove HKCR\.pdmod (the file extension itself)
# in case other handlers (anti-virus, file managers) registered against
# it. Removing only the property-handler shellex binding is safe.

Write-Host "Unregistration complete."
Write-Host ""
Write-Host "To pick up the change in Explorer immediately, run:"
Write-Host "  taskkill /f /im explorer.exe; start explorer.exe"
