@echo off
:: Please save as GBK
:: 用法: call build_test.bat <platform_arch>
::   示例: call build_test.bat windows_amd64
:: 行为: 编译 tests\test.c 并链接 libCrystalStd.a，产出 out\<platform_arch>_bin\test.exe
setlocal enabledelayedexpansion

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

echo 编译测试程序...
gcc tests\test.c -o "%BIN_DIR%\test.exe" -Iinclude -fexec-charset=GBK -Wl,--whole-archive "%LIB%" -Wl,--no-whole-archive
if errorlevel 1 (
    echo [错误] 编译测试程序失败。
    exit /b 1
)
echo 已生成测试程序：%BIN_DIR%\test.exe
