@echo off
:: Please save as GBK
setlocal enabledelayedexpansion

:: 平台信息检测识别
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"

echo 当前平台为：【%PLATFORM_STR%】

:: 构建模式：仅显式传入 release 时启用发行构建
set "BUILD_MODE=DEBUG"
set "CC_BUILD_CFLAGS=-O0 -g3 -Wall -Wextra -fexec-charset=GBK"
if /i "%~1"=="release" (
    set "BUILD_MODE=RELEASE"
    set "CC_BUILD_CFLAGS=-O2 -DNDEBUG -DCC_RELEASE -Wall -Wextra -Werror -fexec-charset=GBK"
) else (
    if not "%~1"=="" echo [提示] 未知构建模式：%~1，按 DEBUG 构建。
)
echo 构建模式：【%BUILD_MODE%】
echo 编译选项：%CC_BUILD_CFLAGS%

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

:: 打生成静态库
echo 生成静态库...
call scripts\build_lib.bat "%PLATFORM_STR%" "%CC_BUILD_CFLAGS%"
if errorlevel 1 (
    echo [错误] 打包静态库失败，中止。
    exit /b 1
)

:: 编译测试程序
echo 生成测试程序...
call scripts\build_test.bat "%PLATFORM_STR%" "%CC_BUILD_CFLAGS%"
if errorlevel 1 (
    echo [错误] 编译测试程序失败，中止。
    exit /b 1
)
:: Compile demo
echo 生成演示程序...
call scripts\build_demo.bat "%PLATFORM_STR%" "%CC_BUILD_CFLAGS%"
if errorlevel 1 (
    echo [错误] 编译演示程序失败，中止。
    exit /b 1
)
