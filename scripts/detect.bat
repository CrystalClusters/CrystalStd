@echo off
:: Please save as GBK

:: 专用于检测平台信息，echo返回拼接好的平台描述串

:: 依据 PROCESSOR_ARCHITECTURE 判断 CPU 指令集架构
set PLATFORM=windows
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
) else if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=amd64
) else (
    set ARCH=unknown
)

echo %PLATFORM%_%ARCH%