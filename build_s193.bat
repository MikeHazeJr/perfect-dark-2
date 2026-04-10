@echo off
set TMP=C:\Users\mikeh\AppData\Local\Temp
set TEMP=C:\Users\mikeh\AppData\Local\Temp
set TMPDIR=C:\Users\mikeh\AppData\Local\Temp
set USERPROFILE=C:\Users\mikeh
set MSYSTEM=MINGW64
set MINGW_PREFIX=/mingw64
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;C:\Windows\system32
cd /D C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike
C:\msys64\usr\bin\make.exe -C build/client -j8
