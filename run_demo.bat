@echo off
:: Please save as GBK
setlocal enabledelayedexpansion

:: Detect platform
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"
set "BIN_DIR=out\%PLATFORM_STR%_bin"

if "%~1"=="" goto :menu
if /i "%~1"=="--list" goto :list_ok

set "DEMO=%~1"
set "EXE=%BIN_DIR%\%DEMO%_demo.exe"
if exist "%EXE%" (
    "%EXE%"
    exit /b %errorlevel%
)

echo [错误] 未找到演示程序: %EXE%
goto :list_fail

:menu
set "COUNT=0"
for /f "delims=" %%F in ('dir /b /a-d /on "%BIN_DIR%\*_demo.exe" 2^>nul') do (
    set /a COUNT+=1
    set "DEMO[!COUNT!]=%%~nF"
)
if "!COUNT!"=="0" (
    echo [错误] 没有可运行的演示程序，请先构建。
    exit /b 1
)
echo.
echo 可用演示：
for /l %%I in (1,1,!COUNT!) do call :show_item %%I
echo   0. 退出
echo.
set "CHOICE="
set /p "CHOICE=请选择要运行的演示（直接回车退出）："
if not defined CHOICE exit /b 0
set "CHOICE=!CHOICE: =!"
if not defined CHOICE exit /b 0
if /i "!CHOICE!"=="q" exit /b 0
if /i "!CHOICE!"=="quit" exit /b 0
if "!CHOICE!"=="0" exit /b 0
for /f "delims=0123456789" %%A in ("!CHOICE!") do goto :invalid
set "NUM=!CHOICE!"
set /a NUM=NUM+0 2>nul
if !NUM! lss 1 goto :invalid
if !NUM! gtr !COUNT! goto :invalid
set "SELECTED=!DEMO[%NUM%]!"
if not defined SELECTED goto :invalid
call "%BIN_DIR%\!SELECTED!.exe"
exit /b %errorlevel%

:show_item
echo   %1. !DEMO[%1]!
exit /b 0

:invalid
echo [错误] 无效选择: !CHOICE!
exit /b 1

:list_ok
echo 可用演示：
for %%F in ("%BIN_DIR%\*_demo.exe") do if exist "%%~F" echo   %%~nF
exit /b 0

:list_fail
echo 可用演示：
for %%F in ("%BIN_DIR%\*_demo.exe") do if exist "%%~F" echo   %%~nF
exit /b 1