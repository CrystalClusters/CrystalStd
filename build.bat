@echo off
:: Please save as GBK
setlocal enabledelayedexpansion

:: 平台信息检测识别
for /f "delims=" %%i in ('call "%~dp0scripts\detect.bat"') do set "PLATFORM_STR=%%i"

echo 当前平台为：【%PLATFORM_STR%】

:: 工具链检测：未通过则中止构建
set CHECK=%~dp0scripts\check.bat
if not exist %CHECK% (
    echo [错误] 找不到工具链检测脚本: %CHECK%
    exit /b 1
)
call %CHECK%
if errorlevel 1 (
    echo [错误] 工具链检测未通过，中止构建。
    exit /b 1
)

::拼接工具路径
set FILTER=tools\%PLATFORM_STR%\filter.exe
echo %FILTER%

::创建输出目录
set OBJ_DIR=out\obj
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

::逐个编译obj
echo 开始编译源文件...
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

:: 打包静态库（交由 build_lib.bat）
echo 生成静态库...
call scripts\build_lib.bat %PLATFORM_STR%
if errorlevel 1 (
    echo [错误] 打包静态库失败，中止。
    exit /b 1
)

:: 编译测试程序
echo 生成测试程序...
call scripts\build_test.bat %PLATFORM_STR%
if errorlevel 1 (
    echo [错误] 编译测试程序失败，中止。
    exit /b 1
)
