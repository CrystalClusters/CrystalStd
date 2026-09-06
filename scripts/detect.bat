@echo off
:: Please save as GBK

set PLATFORM=windows
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
) else if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=amd64
) else (
    set ARCH=unknown
)

echo %PLATFORM%_%ARCH%