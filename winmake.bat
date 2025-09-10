@echo off
setlocal enabledelayedexpansion

:: === CONFIGURATION ==={

:: -DTOKUI_ASSERT => Enables all internal asserts inside Tokudae.
:: -DTOKUI_TRACE_EXEC => Traces bytecode execution (including stack state).
:: -DTOKUI_DISASSEMBLE_BYTECODE => Disassembles precompiled chunks.
:: -DTOKUI_EMERGENCYGCTESTS => Forces an emergency collection at every single allocation.
:: -DTOKUI_HARDMEMTESTS => Forces a full collection at all points where the collector can run.
:: -DTOKUI_HARDSTACKTESTS => Forces a reallocation of the stack at every point where the stack can be reallocated.

set LOGFILE=log.txt

:: if you change this, then also change the ':installfiles' subroutine
set COPYCMD=Copy-Item

:: Version and release
set V=1.0
set R=!V!.0

:: Paths
set INSTALL_ROOT=C:\Program Files\tokudae\!V!
set INSTALL_BIN=!INSTALL_ROOT!\bin
set INSTALL_INC=!INSTALL_ROOT!\include
set INSTALL_LIB=!INSTALL_ROOT!\lib
set INSTALL_DOC=!INSTALL_ROOT!\doc
set INSTALL_TMOD=!INSTALL_ROOT!\tokudae
set INSTALL_CMOD=!INSTALL_LIB!\tokudae

:: Targets
set TOKUDAE_T=tokudae.exe
set TOKUDAE_A=tokudae1.dll
set TOKUDAE_E=tokudae1.exp
set TOKUDAE_L=tokudae1.lib

:: Warning flags
set WDISABLED=/wd4324 /wd4310 /wd4709 /wd4334
set WARNINGS=/W4 /WX !WDISABLED!

:: User flags
set MYCFLAGS=-DTOKU_BUILD_AS_DLL -DTOKUI_ASSERT
set MYLDFLAGS=
set MYLIBS=

:: Compiler, linker and build flags
set CC=cl
set CLINK=link
set CFLAGS=/c /nologo /MD /O2 !WARNINGS! !MYCFLAGS!
set LDFLAGS=!MYLDFLAGS!
set LIBS=!MYLIBS!

:: Special flags for compiler modules
set CMCFLAGS=

:: Core object files
set CORE_O=src\tapi.obj src\tlist.obj src\tcode.obj src\tdebug.obj src\tfunction.obj
set CORE_O=!CORE_O! src\tgc.obj src\ttable.obj src\tlexer.obj src\tmem.obj src\tmeta.obj
set CORE_O=!CORE_O! src\tobject.obj src\tparser.obj src\tvm.obj src\tprotected.obj
set CORE_O=!CORE_O! src\treader.obj src\tstate.obj src\tstring.obj src\ttrace.obj
set LIB_O=src\tokudaeaux.obj src\tbaselib.obj src\tloadlib.obj src\tokudaelib.obj src\tstrlib.obj
:: Standard library object files
set LIB_O=!LIB_O! src\tmathlib.obj src\tiolib.obj src\toslib.obj src\treglib.obj src\tdblib.obj
set LIB_O=!LIB_O! src\tlstlib.obj src\tutf8lib.obj
:: Core and standard library object files
set BASE_O=!CORE_O! !LIB_O!
:: Standalone interpreter object file
set TOKUDAE_O=src\tokudae.obj
:: All object files
set ALL_O=!BASE_O! !TOKUDAE_O!

:: All targets
set ALL_T=!TOKUDAE_A! !TOKUDAE_T! !TOKUDAE_E! !TOKUDAE_L!

:: Files to install
set TO_BIN=!TOKUDAE_T!
set TO_INC=src\tokudae.h src\tokudaeconf.h src\tokudaelib.h src\tokudaeaux.h
set TO_INC=!TO_INC! src\tokudaelimits.h src\tokudae.hpp
set TO_LIB=!TOKUDAE_A! !TOKUDAE_L! !TOKUDAE_E!
set TO_DOC=doc\tokudae.1 doc\manual.html doc\manual.css doc\contents.html doc\EBNF.txt

:: }=== END OF CONFIGURATION ==={


:: }=== MAIN ==={

echo BEGIN >> !LOGFILE!
:: handle commands
if "%1"=="" goto build;
if "%1"=="install" goto install;
if "%1"=="clean" goto clean;
:: otherwise invalid command
echo Usage: winmake.bat [clean | install]
echo - if no arguments are provided, this recompiles and builds the DLL and the executable
echo - 'install' installs the targets and header files onto the system (skips build)
echo - 'clean' removes build artifacts such as .obj files and build targets
exit /b 1

:: build all
:build
:: generate source file names from object file names
set SRCFILES=
for %%f in (!ALL_O!) do (
    set "filename=%%~nf"
    set "SRCFILES=!SRCFILES! src\!filename!.c"
)
:: compile the source files
for %%f in (!SRCFILES!) do (
    set tempflags=
    if %%f=="tlexer.c" { set tempflags=!CMCFLAGS! }
    if %%f=="tparser.c" { set tempflags=!CMCFLAGS! }
    if %%f=="tcode.c" { set tempflags=!CMCFLAGS! }
    call :log "compiling %%f..."
    set outfile=src\%%~nf
    !CC! !CFLAGS! !tempflags! %%f /Fo!outfile!.obj >> !LOGFILE!
    if errorlevel 1 (
	call :logerror "failed to compile %%f"
	echo END >> !LOGFILE!
        exit /b 1
    )
)
call :log "compilation complete"
:: link DLL
call :log "linking !TOKUDAE_A!..."
!CLINK! /NOLOGO /OUT:!TOKUDAE_A! /DLL !LDFLAGS! !LIBS! !BASE_O! >> !LOGFILE!
if errorlevel 1 (
    call :logerror "DLL linking failed"
    echo END >> !LOGFILE!
    exit /b 1
)
:: link executable
call :log "linking !TOKUDAE_T!..."
!CLINK! /NOLOGO /OUT:!TOKUDAE_T! !TOKUDAE_O! !TOKUDAE_L! !LDFLAGS! !LIBS! >> !LOGFILE!
if errorlevel 1 (
    call :logerror "executable linking failed"
    echo END >> !LOGFILE!
    exit /b 1
)
call :log "build complete (!TOKUDAE_T!, !TOKUDAE_A!, !TOKUDAE_L!, !TOKUDAE_E!)"
goto end

:: install headers, libraries and binaries
:install
call :log "installing into !INSTALL_ROOT!"
:: create missing directories
if not exist "!INSTALL_ROOT!" mkdir "!INSTALL_ROOT!"
if not exist "!INSTALL_BIN!" mkdir "!INSTALL_BIN!"
if not exist "!INSTALL_INC!" mkdir "!INSTALL_INC!"
if not exist "!INSTALL_LIB!" mkdir "!INSTALL_LIB!"
if not exist "!INSTALL_DOC!" mkdir "!INSTALL_DOC!"
if not exist "!INSTALL_TMOD!" mkdir "!INSTALL_TMOD!"
if not exist "!INSTALL_CMOD!" mkdir "!INSTALL_CMOD!"
:: install files
call :installfiles "!INSTALL_BIN!" !TO_BIN! 
call :installfiles "!INSTALL_INC!" !TO_INC! 
call :installfiles "!INSTALL_LIB!" !TO_LIB! 
call :installfiles "!INSTALL_DOC!" !TO_DOC! 
call :log "installation complete"
goto end

:installfiles
set dest=!%~1!
:nextarg
if "%2"=="" goto :eof
call :log "installing (%2) into (!dest!)..."
powershell -Command "Copy-Item -Path '%2' -Destination '!dest!' >> !LOGFILE!"
if errorlevel 1 (
    echo END >> !LOGFILE!
    call :logerror "error while trying to install '%2'"
    exit 1
)
shift
goto nextarg;
goto :eof

:: clean build artifacts
:clean
call :log "cleaning build artifacts..."
del /q !ALL_O! 2>nul
del /q !ALL_T! 2>nul
call :log "clean complete"
goto end

:: log routine
:log
echo [INFO]: %~1
goto :eof

:: log error routine
:logerror
echo [ERROR]: %~1 (check log file '!LOGFILE!' if error output is missing)
goto :eof

:: }=== END OF MAIN ===

:end
echo END >> !LOGFILE!
endlocal
