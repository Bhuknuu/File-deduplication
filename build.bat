@echo off
REM ============================================================================
REM File Deduplication System - Build Script
REM Windows Batch Script for Compiling C Project
REM 
REM Compiler Flags:
REM   -Wall -Wextra     : Enable comprehensive warnings
REM   -std=c11          : C11 standard
REM   -O2               : Optimization level 2
REM   -Wno-unknown-pragmas    : Suppress MSVC pragma warnings (GCC doesn't support)
REM   -Wno-unused-parameter   : Suppress callback function parameter warnings
REM ============================================================================

setlocal enabledelayedexpansion
set "PROJECT_NAME=FileDeduplication"
set "OUTPUT_DIR=build"
set "OUTPUT_EXE=%OUTPUT_DIR%\%PROJECT_NAME%.exe"
set "LOG_FILE=%OUTPUT_DIR%\build.log"

REM ============================================================================
REM Color codes for console output
REM ============================================================================
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
    echo Please install MinGW or GCC and add it to PATH
    pause
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
    if errorlevel 1 (
        echo %ERROR% Failed to create build directory
        exit /b 1
    )
    echo %SUCCESS% Build directory created
) else (
    echo %INFO% Build directory already exists
)
echo.

REM ============================================================================
REM Clean previous build artifacts (optional)
REM ============================================================================
echo %INFO% Cleaning previous build artifacts...
if exist "%OUTPUT_EXE%" (
    del "%OUTPUT_EXE%" 2>nul
    if errorlevel 1 (
        echo %WARNING% Could not delete previous executable
    ) else (
        echo %SUCCESS% Cleaned previous executable
    )
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
echo.

set "COMPILE_ERROR=0"
set "OBJECT_FILES="

REM Compile each source file
for %%F in (action.c filter.c gui_win32.c Traversal.c features.c) do (
    set "OBJ_FILE=%OUTPUT_DIR%\%%~nF.o"
    echo %INFO% Compiling %%F...
    gcc %CFLAGS% -c "%%F" -o "!OBJ_FILE!" >> "%LOG_FILE%" 2>&1
    if errorlevel 1 (
        echo %ERROR% Compilation failed for %%F
        set "COMPILE_ERROR=1"
    ) else (
        if exist "!OBJ_FILE!" (
            echo %SUCCESS% %%F compiled successfully
            set "OBJECT_FILES=!OBJECT_FILES! "!OBJ_FILE!"
        ) else (
            echo %ERROR% Object file not created for %%F: !OBJ_FILE!
            set "COMPILE_ERROR=1"
        )
    )
)

if %COMPILE_ERROR% equ 1 (
    echo.
    echo %ERROR% Compilation errors detected!
    echo Showing last 50 lines of log:
    echo ============================================================================
    for /f "skip=1 tokens=*" %%A in ('find /c /v "" "%LOG_FILE%"') do set "LINE_COUNT=%%A"
    setlocal enabledelayedexpansion
    if !LINE_COUNT! gtr 50 (
        set /a START_LINE=!LINE_COUNT!-50
    ) else (
        set START_LINE=1
    )
    endlocal
    type "%LOG_FILE%"
    pause
    exit /b 1
)

echo.
echo %SUCCESS% All source files compiled successfully
echo %INFO% Object files: %OBJECT_FILES%
echo.

REM ============================================================================
REM Link object files
REM ============================================================================
echo %INFO% Linking object files...
if not exist "%OUTPUT_DIR%\action.o" (
    echo %ERROR% Object file not found: %OUTPUT_DIR%\action.o
    dir "%OUTPUT_DIR%\*.o" 2>nul || echo No object files found!
    exit /b 1
)

gcc "%OUTPUT_DIR%\action.o" "%OUTPUT_DIR%\filter.o" "%OUTPUT_DIR%\gui_win32.o" "%OUTPUT_DIR%\Traversal.o" "%OUTPUT_DIR%\features.o" -o "%OUTPUT_EXE%" %LFLAGS% >> "%LOG_FILE%" 2>&1

if errorlevel 1 (
    echo %ERROR% Linking failed!
    echo Showing compilation and linking errors:
    echo ============================================================================
    type "%LOG_FILE%"
    echo ============================================================================
    echo Checking object files in %OUTPUT_DIR%:
    dir "%OUTPUT_DIR%\*.o" 2>nul || echo No object files found!
    pause
    exit /b 1
)

echo %SUCCESS% Linking completed successfully
echo.

REM ============================================================================
REM Verify executable was created
REM ============================================================================
if not exist "%OUTPUT_EXE%" (
    echo %ERROR% Executable was not created
    exit /b 1
)

for %%F in ("%OUTPUT_EXE%") do set "EXE_SIZE=%%~zF"
echo %SUCCESS% Executable created: %OUTPUT_EXE% (Size: %EXE_SIZE% bytes)
echo.

REM ============================================================================
REM Display summary and offer to run
REM ============================================================================
echo %SUCCESS% Build completed successfully!
echo.
echo ============================================================================
echo Build Summary:
echo   Project: %PROJECT_NAME%
echo   Output: %OUTPUT_EXE%
echo   Size: %EXE_SIZE% bytes
echo   Log: %LOG_FILE%
echo ============================================================================
echo.

REM ============================================================================
REM Display compilation warnings (if any)
REM ============================================================================
findstr /c:"warning:" "%LOG_FILE%" >nul 2>&1
if not errorlevel 1 (
    echo %WARNING% Compilation warnings detected (non-fatal):
    echo ============================================================================
    findstr /c:"warning:" "%LOG_FILE%"
    echo ============================================================================
    echo Review these warnings and fix them in the source code for better quality.
    echo.
)

REM ============================================================================
REM Prompt user to run executable
REM ============================================================================
set /p "RUN_PROG=Would you like to run the program now? (Y/N): "
if /i "%RUN_PROG%"=="Y" (
    echo.
    echo %INFO% Launching program...
    "%OUTPUT_EXE%"
) else (
    echo %INFO% Skipped program execution
    echo To run later, execute: %OUTPUT_EXE%
)

echo.
echo %INFO% Build script completed
pause
exit /b 0
