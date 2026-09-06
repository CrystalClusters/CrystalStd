@echo off
:: Please save as GBK

:: 平台信息检测识别
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"

out\%PLATFORM_STR%_bin\test.exe