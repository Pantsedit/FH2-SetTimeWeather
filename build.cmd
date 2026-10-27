@echo off
rem Builds the Set Time ^& Weather FH2 plugin (TimeLock.dll) with Zig 0.15.2 (the same compiler Extra Options uses).
rem Usage: build.cmd [C:\path\to\zig.exe]     (default: zig on PATH)
rem Output: release\Set Time ^& Weather FH2\plugins\TimeLock.dll and tests\offline_test.exe
setlocal
set ZIG=%~1
if "%ZIG%"=="" set ZIG=zig
set "OUT=%~dp0release\Set Time & Weather FH2\plugins"
if not exist "%OUT%" mkdir "%OUT%"
"%ZIG%" cc -shared -O2 -target x86_64-windows-gnu -Wall -Wextra -Wno-missing-field-initializers -Werror ^
  -o "%OUT%\TimeLock.dll" "%~dp0src\timelock.c" "%~dp0src\config.c" "%~dp0src\clock_hold.c" "%~dp0src\weather_hold.c" "%~dp0src\settings_page.c" "%~dp0src\entry_hook.c"
if errorlevel 1 exit /b 1
del "%OUT%\TimeLock.pdb" "%OUT%\TimeLock.lib" 2>nul
echo Built release\Set Time ^& Weather FH2\plugins\TimeLock.dll
rem Offline test harness. Run the tests with:  python tests\run_tests.py "<your FH2 Mount folder>"
"%ZIG%" cc -O1 -target x86_64-windows-gnu -Wall -Wextra -Wno-missing-field-initializers -Werror -o "%~dp0tests\offline_test.exe" ^
  "%~dp0tests\offline_test.c" "%~dp0src\config.c" "%~dp0src\clock_hold.c" "%~dp0src\weather_hold.c" "%~dp0src\settings_page.c" "%~dp0src\entry_hook.c"
if errorlevel 1 exit /b 1
del "%~dp0tests\offline_test.pdb" "%~dp0tests\offline_test.lib" 2>nul
echo Built tests\offline_test.exe
