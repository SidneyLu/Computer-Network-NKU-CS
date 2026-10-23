@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
msbuild "%~dp0Chat.sln" /restore /p:Configuration=Release /p:Platform=x64 /m /v:m || exit /b 1
echo.
echo Server: %~dp0bin\chat_server\chat_server.exe
echo Client: %~dp0bin\chat_client\chat_client.exe
