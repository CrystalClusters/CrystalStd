@echo off
:: Please save as GBK
setlocal enabledelayedexpansion
:: 切换到项目根目录（假设本脚本位于 scripts/ 下）
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_test.bat ^<platform_arch^>
    exit /b 2
)
set "PFX=%~1"
set "BIN_DIR=out\%PFX%_bin"
set "LIB=%BIN_DIR%\libCrystalStd.a"

if not exist "%LIB%" (
    echo [错误] 找不到静态库: %LIB%，请先执行 build_lib
    exit /b 1
)
if not exist "tests\test.c" (
    echo [错误] 找不到测试源文件: tests\test.c
    exit /b 1
)

:: 设置 filter 工具路径
set "FILTER=tools\%PFX%\filter.exe"
if not exist "%FILTER%" (
    echo [错误] 找不到 filter 工具: %FILTER%
    popd
    exit /b 1
)

:: 收集源码
set SRCS=
for /f "delims=" %%s in ('"%FILTER%" -./tests -*.c') do set "SRCS=!SRCS! %%s"

gcc !SRCS! -o "%BIN_DIR%\test.exe" -Iinclude -fexec-charset=GBK -Wl,--whole-archive "%LIB%" -Wl,--no-whole-archive
if errorlevel 1 (
    echo [错误] 编译测试程序失败。
    exit /b 1
)
echo 已生成测试程序：%BIN_DIR%\test.exe
