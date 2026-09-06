@echo off
:: Please save as GBK
setlocal enabledelayedexpansion
:: 切换到项目根目录（假设本脚本位于 scripts/ 下）
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_lib.bat ^<platform_arch^>
    exit /b 2
)
set PFX=%~1

set OBJ_DIR=out\obj
set BIN_DIR=out\%PFX%_bin
set FILTER=tools\%PFX%\filter.exe

::创建输出目录
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if not exist "%FILTER%" (
    echo [错误] 找不到 filter 工具: %FILTER%
    exit /b 1
)

::逐个编译obj
for /f "delims=" %%s in ('"%FILTER%" -./src -*.c') do (
    set name=%%s
    set name=!name:./=!
    set name=!name:.c=.o!
    set name=!name:/=_!
    echo 编译 %%s -^> %OBJ_DIR%\!name!
    gcc -c %%s -o %OBJ_DIR%\!name! -Iinclude -Isrc -fexec-charset=GBK
    if errorlevel 1 (
        echo [错误] 编译失败: %%s
        exit /b 1
    )
)

set OBJ_LIST=
for /f "delims=" %%o in ('"%FILTER%" -./out/obj -*.o') do set "OBJ_LIST=!OBJ_LIST! %%o"
if not defined OBJ_LIST (
    echo [错误] 未找到 .o 目标文件，中止打包。
    exit /b 1
)
ar rcs "%BIN_DIR%\libCrystalStd.a" %OBJ_LIST%
echo 已生成静态库：%BIN_DIR%\libCrystalStd.a
