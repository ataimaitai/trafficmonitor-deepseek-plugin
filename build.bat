@echo off
REM ============================================================
REM  DeepSeekPlugin build script (MinGW-w64 g++, 64-bit)
REM  Output: build\DeepSeekPlugin.dll + build\tester.exe
REM
REM  For 32-bit TrafficMonitor, rebuild with a 32-bit toolchain,
REM  or with MSVC:
REM    cl /LD /utf-8 /EHsc /O2 /DUNICODE /D_UNICODE /Iinclude ^
REM       src\*.cpp /Fe:DeepSeekPlugin.dll ^
REM       /link winhttp.lib shell32.lib gdi32.lib user32.lib comdlg32.lib
REM ============================================================
setlocal
cd /d %~dp0

set "GPP=D:\AppData\mingw64\bin\g++.exe"
if not exist "%GPP%" set "GPP=g++"

if not exist build mkdir build

echo [1/2] Building DeepSeekPlugin.dll ...
"%GPP%" -shared -O2 -std=c++17 -static -DUNICODE -D_UNICODE -Iinclude src\DeepSeekPlugin.cpp src\HttpClient.cpp src\Holidays.cpp src\Credentials.cpp src\OptionsDialog.cpp -o build\DeepSeekPlugin.dll -lwinhttp -lshell32 -lgdi32 -luser32 -lkernel32 -lcomdlg32
if errorlevel 1 (
    echo DLL build FAILED
    exit /b 1
)

echo [2/2] Building tester.exe ...
"%GPP%" -O2 -std=c++17 -static -DUNICODE -D_UNICODE -Iinclude tester\tester.cpp -o build\tester.exe
if errorlevel 1 (
    echo Tester build FAILED
    exit /b 1
)

echo.
echo BUILD OK: build\DeepSeekPlugin.dll  build\tester.exe
endlocal
