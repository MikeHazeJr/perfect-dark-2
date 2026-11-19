#Requires -Version 5.1
# B-1168: checks the release script's ROM-derived content rule without running
# the release script. The two functions are taken from release.ps1's syntax
# tree and defined here; a temporary tree under the system temp folder holds
# one file of each kind. Nothing outside that folder is written, and the folder
# is removed at the end.
#   powershell -NoProfile -File devtools/test-release-byor.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$release = Join-Path $PSScriptRoot 'release.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($release, [ref]$tokens, [ref]$errors)
if (@($errors).Count) { throw "release.ps1 does not parse: $($errors[0].Message)" }
foreach ($name in 'Get-ReleaseRomDerivedReason', 'Assert-ReleaseHasNoRomDerivedContent') {
    $fn = $ast.Find({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
    if (-not $fn) { throw "release.ps1 has no function $name" }
    . ([scriptblock]::Create($fn.Extent.Text))
}

$checks = 0
function Check([bool]$Ok, [string]$Label) {
    if (-not $Ok) { throw "FAILED: $Label" }
    $script:checks++
}

function New-Archive([string]$Path, [hashtable]$Entries) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $zip = [System.IO.Compression.ZipFile]::Open($Path, 'Create')
    try {
        foreach ($key in $Entries.Keys) {
            $writer = New-Object System.IO.StreamWriter(($zip.CreateEntry($key)).Open())
            try { $writer.Write([string]$Entries[$key]) } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function New-Text([string]$Path, [string]$Text) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    [IO.File]::WriteAllText($Path, $Text)
}

$root = Join-Path ([IO.Path]::GetTempPath()) ('pd2-release-byor-' + [guid]::NewGuid().ToString('N'))
try {
    $dirty = Join-Path $root 'dirty'
    $cases = @(
        @{ Rel = 'ntsc-final/files/Pa51boardZ.bin'; Kind = 'text'; Expect = 'raw ROM file or segment' },
        @{ Rel = 'data/ntsc-final/segs/fonts.seg'; Kind = 'text'; Expect = 'raw ROM file or segment' },
        @{ Rel = 'ntsc-final/meshes/.pdextract-cache'; Kind = 'text'; Expect = 'extraction cache stamp' },
        @{ Rel = 'ntsc-final/arenas/.pdextract-clean'; Kind = 'text'; Expect = 'extraction cache stamp' },
        @{ Rel = 'ntsc-final/arenas/.pdextract-stage-textures'; Kind = 'text'; Expect = 'extraction cache stamp' },
        @{ Rel = 'ntsc-final/arenas/.pdextract-stage-textures.pd2tmp-4242-7'; Kind = 'text'; Expect = 'extraction cache stamp' },
        @{ Rel = 'ntsc-final/scenarios/base_scenario_extraction.pdscenario.pd2repair-4242-34.tmp'; Kind = 'text'; Expect = 'temporary stage repair archive' },
        @{ Rel = 'ntsc-final/meshes/base_model_a51board.pdmesh'; Kind = 'zip';
           Entries = @{ 'mesh.ini' = 'x'; '_meta/provenance.json' = '{"tool": "romextract_pdmesh"}' };
           Expect = 'ROM extraction output (romextract_pdmesh)' },
        @{ Rel = 'ntsc-final/arenas/base_arena_old.pdarena'; Kind = 'zip';
           Entries = @{ 'arena.ini' = 'x' }; Expect = 'base archive without provenance' },
        @{ Rel = 'ntsc-final/ui/ui_stars.pdui'; Kind = 'zip';
           Entries = @{ 'ui.ini' = 'x'; '_meta/provenance.json' = '{"tool": "pdguiThemeEmitPduiZips"}' }; Expect = $null },
        @{ Rel = 'ntsc-final/ui/ui_plain.pdui'; Kind = 'zip'; Entries = @{ 'ui.ini' = 'x' }; Expect = $null },
        @{ Rel = 'pd.ini'; Kind = 'text'; Expect = $null },
        @{ Rel = 'put_your_rom_here.txt'; Kind = 'text'; Expect = $null }
    )
    foreach ($case in $cases) {
        $path = Join-Path $dirty $case.Rel
        if ($case.Kind -eq 'zip') { New-Archive $path $case.Entries } else { New-Text $path 'content' }
        $reason = Get-ReleaseRomDerivedReason -Rel $case.Rel -FullName $path
        Check ($reason -eq $case.Expect) ("{0} -> '{1}' (got '{2}')" -f $case.Rel, $case.Expect, $reason)
    }

    $refused = $false
    try { Assert-ReleaseHasNoRomDerivedContent -Root $dirty 6>$null } catch {
        $refused = $_.Exception.Message -match 'Release refused \(B-1168, BYOR\): 9 ROM-derived'
    }
    Check $refused 'a dist with ROM-derived files is refused, naming all 9'

    $clean = Join-Path $root 'clean'
    New-Text (Join-Path $clean 'pd.ini') 'content'
    New-Archive (Join-Path $clean 'data/ntsc-final/ui/ui_stars.pdui') @{ 'ui.ini' = 'x'; '_meta/provenance.json' = '{"tool": "pdguiThemeEmitPduiZips"}' }
    Assert-ReleaseHasNoRomDerivedContent -Root $clean 6>$null
    $checks++

    New-Text (Join-Path $clean 'pd.ntsc-final.z64') 'rom'
    $refused = $false
    try { Assert-ReleaseHasNoRomDerivedContent -Root $clean 6>$null } catch { $refused = $true }
    Check $refused 'a dist with a ROM file is refused'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
Write-Output "PASS: $checks release BYOR checks"
