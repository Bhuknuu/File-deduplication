@echo off
REM ============================================================================
REM File Deduplication System - Build Script
REM Windows Batch Script for Compiling C Project (GUI + CLI)
REM ============================================================================

setlocal enabledelayedexpansion
set "PROJECT_NAME=FileDeduplication"
set "OUTPUT_DIR=build"
set "OUTPUT_EXE=%OUTPUT_DIR%\%PROJECT_NAME%.exe"
set "OUTPUT_CLI=%OUTPUT_DIR%\dedup-cli.exe"
set "LOG_FILE=%OUTPUT_DIR%\build.log"

set "INFO=[INFO]"
set "SUCCESS=[SUCCESS]"
set "ERROR=[ERROR]"
set "WARNING=[WARNING]"

echo ============================================================================
echo %PROJECT_NAME% - Build Script
echo ============================================================================
echo Current Directory: %CD%
echo Build Output: %OUTPUT_DIR%
echo ============================================================================
echo.

REM ============================================================================
REM Check for GCC compiler
REM ============================================================================
echo %INFO% Checking for GCC compiler...
gcc --version >nul 2>&1
if errorlevel 1 (
    echo %ERROR% GCC compiler not found in PATH
    echo Please install MinGW-w64 or add GCC to your system PATH
    exit /b 1
)
echo %SUCCESS% GCC compiler found
echo.

REM ============================================================================
REM Create output directory
REM ============================================================================
echo %INFO% Creating build directory...
if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
    echo %SUCCESS% Build directory created
) else (
    echo %INFO% Build directory already exists
)
echo.

REM ============================================================================
REM Set compiler flags
REM ============================================================================
set "CFLAGS=-Wall -Wextra -std=c11 -O2 -D_WIN32_WINNT=0x0601 -Wno-unknown-pragmas -Wno-unused-parameter"
set "LFLAGS=-lcomctl32 -lshell32 -lole32 -luser32 -lgdi32 -lkernel32 -lbcrypt -luuid"

echo %INFO% Compiler flags: %CFLAGS%
echo %INFO% Linker flags: %LFLAGS%
echo.

REM ============================================================================
REM Compile source files
REM ============================================================================
echo %INFO% Starting compilation...
echo %INFO% Log file: %LOG_FILE%
echo. > "%LOG_FILE%"

set "COMPILE_ERROR=0"

for %%F in (action.c filter.c gui_win32.c Traversal.c features.c cli_main.c) do (
    set "OBJ_FILE=%OUTPUT_DIR%\%%~nF.o"
    echo %INFO% Compiling %%F...
    gcc %CFLAGS% -c "%%F" -o "!OBJ_FILE!" >> "%LOG_FILE%" 2>&1
    if errorlevel 1 (
        echo %ERROR% Compilation failed for %%F
        set "COMPILE_ERROR=1"
    ) else (
        if exist "!OBJ_FILE!" (
            echo %SUCCESS% %%F compiled successfully
        ) else (
            echo %ERROR% Object file not created for %%F: !OBJ_FILE!
            set "COMPILE_ERROR=1"
        )
    )
)

if %COMPILE_ERROR% equ 1 (
    echo.
    echo %ERROR% Compilation errors detected!
    type "%LOG_FILE%"
    exit /b 1
)

echo.
echo %SUCCESS% All source files compiled successfully
echo.

REM ============================================================================
REM Link GUI executable
REM ============================================================================
echo %INFO% Linking GUI executable (%OUTPUT_EXE%)...
gcc "%OUTPUT_DIR%\action.o" "%OUTPUT_DIR%\filter.o" "%OUTPUT_DIR%\gui_win32.o" "%OUTPUT_DIR%\Traversal.o" "%OUTPUT_DIR%\features.o" -o "%OUTPUT_EXE%" %LFLAGS% >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo %ERROR% GUI Linking failed!
    type "%LOG_FILE%"
    exit /b 1
)
echo %SUCCESS% GUI Linking completed successfully

REM ============================================================================
REM Link CLI executable
REM ============================================================================
echo %INFO% Linking CLI executable (%OUTPUT_CLI%)...
gcc "%OUTPUT_DIR%\action.o" "%OUTPUT_DIR%\filter.o" "%OUTPUT_DIR%\Traversal.o" "%OUTPUT_DIR%\features.o" "%OUTPUT_DIR%\cli_main.o" -o "%OUTPUT_CLI%" %LFLAGS% >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo %ERROR% CLI Linking failed!
    type "%LOG_FILE%"
    exit /b 1
)
echo %SUCCESS% CLI Linking completed successfully
echo.

REM ============================================================================
REM Verify executables
REM ============================================================================
for %%F in ("%OUTPUT_EXE%") do set "EXE_SIZE=%%~zF"
for %%F in ("%OUTPUT_CLI%") do set "CLI_SIZE=%%~zF"

echo ============================================================================
echo Build Summary:
echo   GUI Executable: %OUTPUT_EXE% (%EXE_SIZE% bytes)
echo   CLI Executable: %OUTPUT_CLI% (%CLI_SIZE% bytes)
echo   Log File:       %LOG_FILE%
echo ============================================================================
echo.

if "%1"=="--no-run" goto build_done
if "%1"=="-n" goto build_done
if "%1"=="cli" goto run_cli
if "%1"=="--cli" goto run_cli
if "%CI%"=="true" goto build_done

set /p "RUN_PROG=Would you like to run the program now? (Y=GUI, C=CLI, N=Exit): "
if /i "%RUN_PROG%"=="Y" (
    echo.
    echo %INFO% Launching GUI program...
    start "" "%OUTPUT_EXE%"
) else if /i "%RUN_PROG%"=="C" (
    goto run_cli
) else (
    echo %INFO% Skipped program execution
)
goto build_done

:run_cli
echo.
"%OUTPUT_CLI%" --help
goto build_done

:build_done
echo %INFO% Build script completed
exit /b 0
