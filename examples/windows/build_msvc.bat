@echo off
rem Build the Windows 10/11 x64 example with MSVC (cl.exe).
rem Run it from "x64 Native Tools Command Prompt for VS". Output goes to build\.
setlocal
where cl >nul 2>&1
if errorlevel 1 (
    echo cl.exe not found. Start this script from the x64 Native Tools Command Prompt for VS.
    exit /b 1
)
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
   /I..\..\common /I..\..\host /I.. /I..\monitor ^
   main_windows.c ..\monitor\example_monitor.c ^
   ..\..\host\scom_host.c ..\..\host\scom_host_posix.c ..\..\host\scom_host_win32.c ^
   ..\..\common\scom_frame.c ..\..\common\scom_crc32.c ^
   winmm.lib /Fo:build\ /Fe:build\example_windows.exe
if errorlevel 1 exit /b 1
echo Built: build\example_windows.exe
echo Run:   build\example_windows.exe COM5
