@echo off
:: Please save as GBK
setlocal enabledelayedexpansion
pushd "%~dp0.." || exit /b 1

if "%~1"=="" (
    echo [错误] 用法: call build_demo.bat ^<platform_arch^> [build_flags]
    popd
    exit /b 2
)
set "PFX=%~1"
set "CC_BUILD_CFLAGS=%~2"
set "BIN_DIR=out\%PFX%_bin"
set "LIB=%BIN_DIR%\libCrystalStd.a"

if not exist "%LIB%" (
    echo [错误] 找不到静态库: %LIB%，请先执行 build_lib
    popd
    exit /b 1
)
if not exist "demo" (
    echo [错误] 找不到演示目录: demo
    popd
    exit /b 1
)

set "COUNT=0"

for /d %%G in ("demo\*") do (
    if /i not "%%~nxG"=="common" (
        if exist "%%~G\main.c" (
            call :build_one "%%~G" "%%~nxG" "%%~G\common" single
            if errorlevel 1 exit /b 1
            set /a COUNT+=1
        ) else (
            for /d %%T in ("%%~G\*") do (
                if /i not "%%~nxT"=="common" (
                    if exist "%%~T\main.c" (
                        call :build_one "%%~T" "%%~nxG_%%~nxT" "%%~G\common" multi
                        if errorlevel 1 exit /b 1
                        set /a COUNT+=1
                    )
                )
            )
        )
    )
)

if "!COUNT!"=="0" (
    echo [错误] 未发现任何演示目标（需要 demo\^<group^>\main.c 或 demo\^<group^>\^<target^>\main.c）
    popd
    exit /b 1
)
echo 演示程序构建完成，共 !COUNT! 个目标。
popd
exit /b 0

:build_one
set "TDIR=%~1"
set "NAME=%~2"
set "CDIR=%~3"
set "MODE=%~4"
set "SRCS="
set "CINC="

if not exist "%TDIR%\main.c" (
    echo [错误] 目标缺少 main.c: %TDIR%
    exit /b 1
)

if /i "%MODE%"=="single" (
    for %%S in ("%TDIR%\*.c") do (
        if exist "%%~S" set SRCS=!SRCS! "%%~S"
    )
) else (
    for /r "%TDIR%" %%S in (*.c) do set SRCS=!SRCS! "%%~S"
)

if exist "%CDIR%" (
    set CINC=-I"%CDIR%"
    for /r "%CDIR%" %%S in (*.c) do set SRCS=!SRCS! "%%~S"
)

if not defined SRCS (
    echo [错误] 目标没有源文件: %TDIR%
    exit /b 1
)

echo 生成演示程序：%NAME%_demo
gcc !SRCS! -o "%BIN_DIR%\%NAME%_demo.exe" -Iinclude -I"%TDIR%" !CINC! %CC_BUILD_CFLAGS% -Wl,--whole-archive "%LIB%" -Wl,--no-whole-archive -lws2_32 -liphlpapi
if errorlevel 1 (
    echo [错误] 生成演示程序失败: %NAME%
    exit /b 1
)
exit /b 0