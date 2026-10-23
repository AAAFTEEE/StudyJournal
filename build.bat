@echo off
rem ============================================================
rem  StudyJournal build script (MinGW-w64 / MSYS2)
rem  Usage: double-click build.bat, or run it from a terminal
rem  Output: build\StudyJournal.exe  +  build\test_core.exe
rem
rem  The exe is built unsigned. Sign it with your own code-signing
rem  certificate before public release if you need SmartScreen trust.
rem ============================================================
setlocal
cd /d "%~dp0"

if not exist build mkdir build

echo [1/3] Compiling resources (Common Controls v6 + DPI manifest) ...
windres res\app.rc -O coff -o build\app_res.o
if errorlevel 1 (
    echo Resource compilation failed. Make sure windres is in PATH.
    exit /b 1
)

echo [2/3] Compiling StudyJournal ...
g++ -std=c++17 -O2 -o build\StudyJournal.exe src\*.cpp build\app_res.o ^
    -mwindows -lcomctl32 -lcomdlg32 -lole32 -lshell32 -ldwmapi -lwininet -lgdi32 -luser32
if errorlevel 1 (
    echo.
    echo Build failed. Make sure g++ is installed and in PATH.
    exit /b 1
)

echo [3/3] Building and running core tests ...
g++ -std=c++17 -O2 -o build\test_core.exe src\encoding.cpp src\storage.cpp src\tasks.cpp tests\test_core.cpp ^
    -lgdi32 -lole32 -lshell32
if errorlevel 1 (
    echo Test build failed.
    exit /b 1
)
build\test_core.exe
if errorlevel 1 (
    echo Core tests failed.
    exit /b 1
)

echo.
echo [OK] build\StudyJournal.exe  (unsigned)
echo      The data directory is created next to the exe on first run.
echo.
endlocal
