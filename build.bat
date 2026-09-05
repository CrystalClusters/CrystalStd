@echo off
:: Please save as GBK
setlocal enabledelayedexpansion

:: 平台识别：Windows 体系
:: 依据 PROCESSOR_ARCHITECTURE 判断 CPU 架构

if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
) else if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=amd64
) else (
    echo [错误] 不支持的 CPU 架构: %PROCESSOR_ARCHITECTURE%
    exit /b 1
)

echo 当前为 Windows 平台，CPU 指令集架构：%ARCH%

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
set FILTER=tools\windows_%ARCH%\filter.exe
echo %FILTER%

::创建输出目录
set OBJ_DIR=out\obj
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

::逐个编译obj
echo 开始编译源文件...
for /f "delims=" %%s in ('"%FILTER%" -./src -*.c') do (
    set name=%%s
    set name=!name:./=!
    set name=!name:.c=.o!
    set name=!name:/=_!
    echo 编译 %%s -^> %OBJ_DIR%\!name!
    gcc -c %%s -o %OBJ_DIR%\!name! -Iinclude -Isrc -fexec-charset=GBK
    if errorlevel 1 (
        echo [错误] 编译失败: %%s
        exit /b 1
    )
)


:: 打包静态库（交由 build_lib.bat）
echo 生成静态库...
call scripts\build_lib.bat windows_%ARCH%
if errorlevel 1 (
    echo [错误] 打包静态库失败，中止。
    exit /b 1
)

:: 编译测试程序
echo 生成测试程序...
call scripts\build_test.bat windows_%ARCH%
if errorlevel 1 (
    echo [错误] 编译测试程序失败，中止。
    exit /b 1
)
