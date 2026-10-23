@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
msbuild "%~dp0Capture.sln" /restore /p:Configuration=Release /p:Platform=x64 /m /v:m || exit /b 1
echo.
echo Capture: %~dp0bin\capture\capture.exe
