@echo off
:: Please save as GBK

setlocal

echo [检测] 正在检测工具链...

:: 1) gcc 是否存在
where gcc >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到编译器 gcc，请先安装 MinGW-w64 等并将其加入 PATH。
    exit /b 1
)

:: 2) ar 是否存在（用于静态库打包）
where ar >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 ar，请先安装 binutils 等并将其加入 PATH。
    exit /b 2
)

:: 3) 打印版本（仅取首行）
set "gccver="
for /f "delims=" %%i in ('gcc --version 2^>nul') do if not defined gccver set "gccver=%%i"
echo [检测] gcc 版本：%gccver%

set "arver="
for /f "delims=" %%i in ('ar --version 2^>nul') do if not defined arver set "arver=%%i"
echo [检测] ar  版本：%arver%

echo [检测] 工具链检测通过。
exit /b 0
