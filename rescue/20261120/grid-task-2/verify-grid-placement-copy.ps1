param(
    [ValidateSet('grid-placement-copy', 'grid-save-stage-copy')]
    [string]$CopyName = 'grid-placement-copy'
)

$ErrorActionPreference = 'Stop'
$repo = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
$copy = Join-Path (Join-Path $PSScriptRoot $CopyName) 'source'
$build = Join-Path $repo '.claude\session-builds\pd1010'
$commands = Get-Content -LiteralPath (Join-Path $build 'compile_commands.json') -Raw | ConvertFrom-Json
$units = @(
    @{ Name = 'port/src/forge_level_io.c'; Command = 'asset_archive_policy.c'; Quote = 'port/src' },
    @{ Name = 'port/fast3d/pdgui_forge_editor.cpp'; Command = 'pdgui_forge_editor.cpp'; Quote = 'port/fast3d' }
)

. (Join-Path $repo 'devtools\_build-env-prelude.ps1')
$copyIncludes = Join-Path $copy 'port\include'
$header = Join-Path $copy 'port\include\forge_level_io.h'
Write-Output "forge_level_io.h SHA256 $((Get-FileHash -LiteralPath $header).Hash)"
foreach ($unit in $units) {
    $command = $commands | Where-Object { $_.file -like "*/$($unit.Command)" } | Select-Object -First 1
    if (-not $command) { throw "Missing recorded production flags for $($unit.Command)" }
    $fullArguments = $command.command -split ' '
    $outputIndex = [Array]::IndexOf($fullArguments, '-o')
    if ($outputIndex -lt 2) { throw 'Recorded production compiler command is incomplete' }
    $compiler = $fullArguments[0]
    $flags = $fullArguments[1..($outputIndex - 1)]
    $quoteIncludes = Join-Path $repo $unit.Quote
    $source = Join-Path $copy $unit.Name
    Write-Output "$($unit.Name) SHA256 $((Get-FileHash -LiteralPath $source).Hash)"
    & $compiler "-I$copyIncludes" @flags -iquote $quoteIncludes -fsyntax-only $source
    if ($LASTEXITCODE -ne 0) { throw "Syntax check failed for $($unit.Name) (exit $LASTEXITCODE)" }
}
Write-Output "Both translation units passed syntax checks for $CopyName."
