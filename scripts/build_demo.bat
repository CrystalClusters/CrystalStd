@echo off
:: Please save as GBK
setlocal enabledelayedexpansion
:: 切换到项目根目录（本脚本位于 scripts/ 下）
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_demo.bat ^<platform_arch^> [build_flags]
    exit /b 2
)
set "PFX=%~1"
set "CC_BUILD_CFLAGS=%~2"
set "BIN_DIR=out\%PFX%_bin"
set "LIB=%BIN_DIR%\libCrystalStd.a"

if not exist "%LIB%" (
    echo [错误] 找不到静态库: %LIB%，请先执行 build_lib
    exit /b 1
)
if not exist "demo\ccfiles_demo.c" (
    echo [错误] 找不到演示源文件: demo\ccfiles_demo.c
    exit /b 1
)

:: 收集源码
set SRCS=
for /r demo %%s in (*.c) do set "SRCS=!SRCS! %%s"

gcc !SRCS! -o "%BIN_DIR%\ccfiles_demo.exe" -Iinclude %CC_BUILD_CFLAGS% -Wl,--whole-archive "%LIB%" -Wl,--no-whole-archive
if errorlevel 1 (
    echo [错误] 编译演示程序失败。
    exit /b 1
)
echo 已生成演示程序：%BIN_DIR%\ccfiles_demo.exe