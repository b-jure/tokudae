@echo off
setlocal enabledelayedexpansion

:: build and source directory
set TOKU_LIB=..\..\tokudae1.lib
set TOKU_DIR=..\..\src

:: log file
set LOGFILE=log.txt

:: warning flags
set WDISABLED=/wd4324 /wd4310 /wd4709 /wd4334
set WARNINGS=/W4 /WX !WDISABLED!

:: compiler, linker and build flags
set CC=cl
set CFLAGS=/nologo /MD /O2 -DTOKU_BUILD_AS_DLL !WARNINGS! /I!TOKU_DIR!

:: object files
set ALL_O=lib1.obj lib11.obj lib2.obj lib21.obj lib22.obj

:: libraries used by the tests
set ALL_A=lib1.dll lib11.dll lib2.dll lib21.dll lib2-v2.dll
set ALL_L=lib1.lib lib11.lib lib2.lib lib21.lib lib2-v2.lib
set ALL_E=lib1.exp lib11.exp lib2.exp lib21.exp lib2-v2.exp

echo BEGIN >> !LOGFILE!
if "%1"=="clean" goto clean;
if "%1"=="clog" goto cleanlog;
if "%1"=="" goto build;
echo Usage: .\winmake.bat [clean]
echo - if no arguments are prrovided, this recompiles and builds the DLLs.
echo - 'clean' removes build artifacts such as .obj files and build targets.
echo - 'clog' removes (cleans) the log file.
exit /b 1

:build
:: build libraries
call :buildlib lib1.dll lib1.c
call :buildlib lib11.dll lib11.c lib1.obj
call :buildlib lib2.dll lib2.c
call :buildlib lib21.dll lib21.c lib2.obj
call :buildlib lib2-v2.dll lib22.c
call :log "build complete"
goto end;

:: routine for linking a library
:buildlib
call :log "building %1"
!CC! !CFLAGS! /LD %2 /link /OUT:%1 !TOKU_LIB! %3 >> !LOGFILE!
if errorlevel 1 (
    call :logerror "%1 build failed"
    echo END >> !LOGFILE!
    exit 1
)
goto :eof

:: clean build artifacts (object files and DLLs)
:clean
call :log "cleaning build artifacts..."
del /q !ALL_O! 2>nul
del /q !ALL_A! 2>nul
del /q !ALL_L! 2>nul
del /q !ALL_E! 2>nul
call :log "clean complete"
goto end;

:cleanlog
call :log "cleaning log file..."
del /q !LOGFILE! 2>nul
call :log "log clean complete"
exit 0

:: log routine
:log
echo [INFO]: %~1
goto :eof

:: log error routine
:logerror
echo [ERROR]: %~1 (check log file '!LOGFILE!')
goto :eof

:end
echo END >> !LOGFILE!
endlocal
