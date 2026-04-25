$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$preludePath = Join-Path $projectRoot "devtools/_build-env-prelude.ps1"
. $preludePath

$clientBuildDir = Join-Path $projectRoot "build/client"
$serverBuildDir = Join-Path $projectRoot "build/server"

Write-Host "=== Building pd_client ==="
cmake --build $clientBuildDir -j4 2>&1 | Select-Object -Last 60

Write-Host "=== Building pd_server ==="
cmake --build $serverBuildDir -j4 2>&1 | Select-Object -Last 30
