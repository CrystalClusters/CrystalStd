@echo off
:: Please save as GBK

:: Detect platform
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"

out\%PLATFORM_STR%_bin\ccfiles_demo.exe
