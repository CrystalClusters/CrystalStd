@echo off
:: Please save as GBK

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
if not exist "%CHECK%" (
    echo [错误] 找不到工具链检测脚本: %CHECK%
    exit /b 1
)
call "%CHECK%"
if errorlevel 1 (
    echo [错误] 工具链检测未通过，中止构建。
    exit /b 1
)

:: 分流到 scripts 中的细分脚本
set SCRIPT=%~dp0scripts\build_windows_%ARCH%.bat
if not exist %SCRIPT% (
    echo [错误] 找不到目标脚本: %SCRIPT%
    exit /b 1
)

call %SCRIPT%