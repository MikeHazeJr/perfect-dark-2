param(
    [ValidateSet("List", "Extract", "Open", "SelfTest")]
    [string]$Mode = "List",
    [string]$ProjectRoot = "",
    [string]$DataRoot = "",
    [string]$OutputRoot = "",
    [string]$Type = "all",
    [string]$Search = "",
    [string[]]$Path = @(),
    [switch]$Json
)

$modulePath = Join-Path $PSScriptRoot "pdxxx-asset-tool.psm1"
Import-Module $modulePath -Force

if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path $PSScriptRoot -Parent
}
try { $ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot) } catch {}
if (-not $DataRoot) { $DataRoot = Get-PdxxxDefaultDataRoot -ProjectRoot $ProjectRoot }
if (-not $OutputRoot) { $OutputRoot = Get-PdxxxDefaultExtractRoot -ProjectRoot $ProjectRoot }

switch ($Mode) {
    "SelfTest" {
        Invoke-PdxxxAssetToolSelfTest
        break
    }
    "Open" {
        Open-PdxxxExtractRoot -Path $OutputRoot
        break
    }
    "Extract" {
        if ($Path.Count -eq 0) { throw "Extract mode requires -Path." }
        $result = Export-PdxxxAssets -Paths $Path -OutputRoot $OutputRoot
        if ($Json) { $result | ConvertTo-Json -Depth 8 } else { "extracted $($result.Count) asset(s) to $($result.Root)" }
        break
    }
    default {
        $items = Get-PdxxxAssetList -Root $DataRoot -Type $Type -Search $Search
        if ($Json) {
            $items | ConvertTo-Json -Depth 6
        } else {
            $items | Format-Table Type, Id, Entries, Meshes, Sources, RelativePath -AutoSize
        }
        break
    }
}
