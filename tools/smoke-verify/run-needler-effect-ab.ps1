#Requires -Version 5.1
<#
.SYNOPSIS
    Runs two ordinary-client Needler effect smokes from distinct editable tints.

.DESCRIPTION
    Generates baseline pink and conspicuous cyan Needler mods from the public
    effect graph source, packs each as a real .pdmod, then runs the normal
    Combat Simulator secondary-projectile collision path. The opt-in renderer
    audit must report the exact authored, prepared, and render RGBA for each
    variant. Generated source/test definitions are temporary; ordinary smoke
    results, logs, and screenshots remain under .claude/smoke-verify-runs.
#>

[CmdletBinding()]
param(
    [int] $Timeout = 180,
    [string] $SourceBinary = "",
    [string] $SourceRom = "",
    [switch] $KeepGeneratedTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$Stamp = "{0:yyyyMMddTHHmmssZ}" -f ((Get-Date).ToUniversalTime())
$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs\needler-effect-ab"
$GeneratedDir = Join-Path $RunRoot ("tests-{0}" -f $Stamp)
$AssetDir = Join-Path $RunRoot ("assets-{0}" -f $Stamp)
$Builder = Join-Path $ProjectRoot "tools\build_needler_mod.py"
$BaseTest = Join-Path $ScriptDir "tests\needler_graph_runtime_visual_smoke.json"

New-Item -ItemType Directory -Path $GeneratedDir -Force | Out-Null
New-Item -ItemType Directory -Path $AssetDir -Force | Out-Null

function ConvertTo-ProjectRelativePath {
    param([Parameter(Mandatory)] [string] $Path)

    $projectFull = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\', '/')
    $full = (Resolve-Path -LiteralPath $Path).Path
    if (-not $full.StartsWith($projectFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "generated fixture escaped project root: $full"
    }
    return $full.Substring($projectFull.Length).TrimStart('\', '/')
}

function New-NeedlerVariant {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $BurstTint,
        [Parameter(Mandatory)] [string] $SparkTint,
        [Parameter(Mandatory)] [string] $ExpectedRgba
    )

    $modDir = Join-Path $AssetDir $Name
    & python $Builder --output-dir $modDir --burst-tint $BurstTint --spark-tint $SparkTint
    if ($LASTEXITCODE -ne 0) {
        throw "Needler public-source generator failed for $Name"
    }

    $definition = Get-Content -LiteralPath $BaseTest -Raw | ConvertFrom-Json
    $definition.scenario_name = "needler_effect_ab_$Name"
    $definition.description = "V-009 edited-value A/B proof ($Name): a generated public Needler tint must reach the normal projectile/effect transaction and production renderer without fallback."
    $definition.timeout_seconds = $Timeout
    $definition.packed_fixtures[0].src = ConvertTo-ProjectRelativePath -Path $modDir
    foreach ($shot in $definition.screenshots) {
        $shot.name = "needler-effect-$Name-$($shot.name)"
    }

    $required = @($definition.assertions.required_lines)
    for ($i = 0; $i -lt $required.Count; $i++) {
        if ($required[$i] -like "SMOKE: scenario=*") {
            $required[$i] = "SMOKE: scenario=needler_effect_ab_$Name"
        } elseif ($required[$i].Contains("EFFECT\.PRESENTATION\.RENDER\.AUDIT")) {
            $required[$i] = "EFFECT\.PRESENTATION\.RENDER\.AUDIT: instance=\d+ snapshots=[1-9]\d* channel=2 shader=needler_pink_burst intensity=[0-9.]+ authored_rgba=$ExpectedRgba effective_rgba=$ExpectedRgba render_rgba=$ExpectedRgba"
        }
    }
    $definition.assertions.required_lines = $required
    $definition.assertions.required_counts = @($definition.assertions.required_counts) + @(
        [ordered]@{
            pattern = "EFFECT\.PRESENTATION\.RENDER\.AUDIT: .*authored_rgba=$ExpectedRgba effective_rgba=$ExpectedRgba render_rgba=$ExpectedRgba"
            min = 1
        }
    )
    $json = $definition | ConvertTo-Json -Depth 20
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        (Join-Path $GeneratedDir "$Name.json"), $json, $utf8NoBom)
}

New-NeedlerVariant -Name "baseline" `
    -BurstTint "1.0,0.4,0.8,1.0" `
    -SparkTint "1.0,0.5,0.85,1.0" `
    -ExpectedRgba "1\.000,0\.400,0\.800,1\.000"
New-NeedlerVariant -Name "cyan" `
    -BurstTint "0.0,1.0,1.0,1.0" `
    -SparkTint "0.1,0.8,1.0,1.0" `
    -ExpectedRgba "0\.000,1\.000,1\.000,1\.000"

$runner = Join-Path $ScriptDir "run.ps1"
try {
    $result = 0
    foreach ($name in @("baseline", "cyan")) {
        $runnerArgs = @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", $runner,
            "-TestsDir", $GeneratedDir,
            "-Test", $name,
            "-Timeout", $Timeout
        )
        if ($SourceBinary) { $runnerArgs += @("-SourceBinary", $SourceBinary) }
        if ($SourceRom) { $runnerArgs += @("-SourceRom", $SourceRom) }
        & powershell @runnerArgs
        $result = $LASTEXITCODE
        if ($result -ne 0) { break }

        $releaseDeadline = (Get-Date).AddSeconds(10)
        while (Get-Process -Name "PerfectDark", "Updater" -ErrorAction SilentlyContinue) {
            if ((Get-Date) -ge $releaseDeadline) {
                throw "previous smoke process did not release before $name reseed"
            }
            Start-Sleep -Milliseconds 100
        }
    }
    exit $result
} finally {
    if (-not $KeepGeneratedTests) {
        Remove-Item -LiteralPath $GeneratedDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $AssetDir -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host ("Generated A/B tests kept at: {0}" -f $GeneratedDir)
        Write-Host ("Generated A/B assets kept at: {0}" -f $AssetDir)
    }
}
