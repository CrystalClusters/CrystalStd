@echo off
:: Please save as GBK
setlocal enabledelayedexpansion

:: 平台信息检测识别
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"

echo 当前平台为：【%PLATFORM_STR%】

:: 工具链检测：未通过则中止构建
set CHECK=%~dp0scripts\check.bat
if not exist %CHECK% (
    echo [错误] 找不到工具链检测脚本: %CHECK%
    exit /b 1
)
call %CHECK%
if errorlevel 1 (
    echo [错误] 工具链检测未通过，中止构建。
    exit /b 1
)

::拼接工具路径
set FILTER=tools\%PLATFORM_STR%\filter.exe
echo %FILTER%

:: 打生成静态库
echo 生成静态库...
call scripts\build_lib.bat %PLATFORM_STR%
if errorlevel 1 (
    echo [错误] 打包静态库失败，中止。
    exit /b 1
)

:: 编译测试程序
echo 生成测试程序...
call scripts\build_test.bat %PLATFORM_STR%
if errorlevel 1 (
    echo [错误] 编译测试程序失败，中止。
    exit /b 1
)
