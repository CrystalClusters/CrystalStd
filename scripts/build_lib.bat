@echo off
:: Please save as GBK
:: 用法: call build_lib.bat <platform_arch>
::   示例: call build_lib.bat windows_amd64
:: 行为: 用 filter 扫描 out\obj 下的 .o，打包为 out\<platform_arch>_bin\libCrystalStd.a
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo [错误] 用法: call build_lib.bat ^<platform_arch^>
    exit /b 2
)
set "PFX=%~1"
set "OBJ_DIR=out\obj"
set "BIN_DIR=out\%PFX%_bin"
set "FILTER=tools\%PFX%\filter.exe"

if not exist "%FILTER%" (
    echo [错误] 找不到 filter 工具: %FILTER%
    exit /b 1
)
if not exist "%OBJ_DIR%" (
    echo [错误] 未找到 obj 目录: %OBJ_DIR%
    exit /b 1
)
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

echo 开始打包静态库...
set "OBJ_LIST="
for /f "delims=" %%o in ('"%FILTER%" -./out/obj -*.o') do set "OBJ_LIST=!OBJ_LIST! %%o"
if not defined OBJ_LIST (
    echo [错误] 未找到 .o 目标文件，中止打包。
    exit /b 1
)
ar rcs "%BIN_DIR%\libCrystalStd.a" %OBJ_LIST%
echo 已生成静态库：%BIN_DIR%\libCrystalStd.a
