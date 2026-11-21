$ErrorActionPreference = 'Stop'
$repo = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
$build = Join-Path $repo '.claude\session-builds\pd1010'
$commands = Get-Content -LiteralPath (Join-Path $build 'compile_commands.json') -Raw | ConvertFrom-Json
$menuCommand = $commands | Where-Object { $_.file -like '*/pdgui_menu_mainmenu.cpp' } | Select-Object -First 1
if (-not $menuCommand) { throw 'No recorded production flags for the main menu' }

. (Join-Path $repo 'devtools\_build-env-prelude.ps1')
$fullArguments = $menuCommand.command -split ' '
$outputIndex = [Array]::IndexOf($fullArguments, '-o')
if ($outputIndex -lt 2) { throw 'Recorded production compiler command is incomplete' }
$compiler = $fullArguments[0]
$flags = $fullArguments[1..($outputIndex - 1)]
$sourceDirectory = Join-Path $PSScriptRoot 'port\fast3d'
$baseIncludes = '-iquote', (Join-Path $repo 'port\fast3d')
foreach ($unit in 'pdgui_grid_browser_data.cpp', 'pdgui_menu_mainmenu.cpp') {
    $source = Join-Path $sourceDirectory $unit
    Write-Output "$unit SHA256 $((Get-FileHash -LiteralPath $source).Hash)"
    & $compiler @flags @baseIncludes -fsyntax-only $source
    if ($LASTEXITCODE -ne 0) { throw "Syntax check failed for $unit (exit $LASTEXITCODE)" }
}
Write-Output 'Both Grid browser translation units passed syntax checks. The browser header static assertions compiled.'
