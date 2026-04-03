$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
$env:TEMP = "C:\Temp"
Write-Host "=== Building pd_client ==="
cmake --build "C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\build\client" -j4 2>&1 | Select-Object -Last 60
Write-Host "=== Building pd_server ==="
cmake --build "C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\build\server" -j4 2>&1 | Select-Object -Last 30
