@echo off
setlocal enabledelayedexpansion

set BUILD=build
set BOOT=boot
set KERNEL=kernel
set CC=i686-elf-gcc
set LD=i686-elf-ld
set NASM=nasm
set PYTHON=python
set CFLAGS=-m32 -ffreestanding -fno-stack-protector -fno-pie -Wall -O2 -I%KERNEL%
set LDFLAGS=-T link.ld -m elf_i386 --oformat binary

if exist tools\mkimage.py  ( set MKIMAGE=tools\mkimage.py  ) else ( set MKIMAGE=mkimage.py  )
if exist tools\addfiles.py ( set ADDFILES=tools\addfiles.py ) else ( set ADDFILES=addfiles.py )
if exist tools\makedisk.py ( set MAKEDISK=tools\makedisk.py ) else ( set MAKEDISK=makedisk.py )

if "%1"=="clean"      goto :clean
if "%1"=="run"        goto :run_only
if "%1"=="addfiles"   goto :addfiles
if "%1"=="rebuild"    goto :rebuild
if "%1"=="resetdisk"  goto :resetdisk
if "%1"=="diskinfo"   goto :diskinfo
goto :build

:: ── build ──────────────────────────────────────────────────────────────────
:: Compiles everything fresh and assembles myos.img.
:: Creates fat12.img if it doesn't exist (first run), otherwise keeps it.
:: Your files on disk are NEVER wiped by a normal build.
:build
if not exist %BUILD% mkdir %BUILD%

:: Create the persistent FAT12 disk image if this is the first build
if not exist %BUILD%\fat12.img (
    echo [0/6] First run - creating persistent FAT12 disk...
    %PYTHON% %MAKEDISK%
    if errorlevel 1 ( echo ERROR: makedisk.py failed & goto :fail )
    echo [0/6] Adding default files to disk...
    %PYTHON% %ADDFILES%
    if errorlevel 1 ( echo ERROR: addfiles.py failed & goto :fail )
)

echo [1/6] Assembling boot.asm...
%NASM% -f bin %BOOT%\boot.asm -o %BUILD%\boot.bin
if errorlevel 1 ( echo ERROR: boot.asm failed & goto :fail )

echo [2/6] Assembling stage2.asm...
%NASM% -f bin %BOOT%\stage2.asm -o %BUILD%\stage2.bin
if errorlevel 1 ( echo ERROR: stage2.asm failed & goto :fail )

echo [3/6] Assembling start.asm...
%NASM% -f elf32 %KERNEL%\start.asm -o %BUILD%\start.o
if errorlevel 1 ( echo ERROR: start.asm failed & goto :fail )

echo [4/6] Compiling C sources...
for %%f in (kernel console pic idt keyboard disk fat12 serial netfs time) do (
    echo     Compiling %%f.c...
    %CC% %CFLAGS% -c %KERNEL%\%%f.c -o %BUILD%\%%f.o
    if errorlevel 1 ( echo ERROR: %%f.c failed & goto :fail )
)

echo [5/6] Linking kernel...
%LD% %LDFLAGS% -o %BUILD%\kernel.bin %BUILD%\start.o %BUILD%\kernel.o %BUILD%\console.o %BUILD%\pic.o %BUILD%\idt.o %BUILD%\keyboard.o %BUILD%\disk.o %BUILD%\fat12.o %BUILD%\serial.o %BUILD%\netfs.o %BUILD%\time.o
if errorlevel 1 ( echo ERROR: Linking failed & goto :fail )

echo [6/6] Assembling disk image...
%PYTHON% %MKIMAGE%
if errorlevel 1 ( echo ERROR: mkimage.py failed & goto :fail )

echo.
echo Build successful!  Image: %BUILD%\myos.img
echo Disk data:         %BUILD%\fat12.img  ^(preserved across builds^)
echo.
echo Next steps:
echo   build.bat run         ^<-- launch QEMU
echo   build.bat addfiles    ^<-- add more files to disk
echo   build.bat diskinfo    ^<-- show disk usage
echo   build.bat resetdisk   ^<-- WIPE disk and start fresh
echo.
goto :end

:: ── rebuild ────────────────────────────────────────────────────────────────
:: Recompile kernel only. Does NOT touch fat12.img at all.
:rebuild
if not exist %BUILD% mkdir %BUILD%

echo [1/5] Assembling boot.asm...
%NASM% -f bin %BOOT%\boot.asm -o %BUILD%\boot.bin
if errorlevel 1 ( echo ERROR: boot.asm failed & goto :fail )

echo [2/5] Assembling stage2.asm...
%NASM% -f bin %BOOT%\stage2.asm -o %BUILD%\stage2.bin
if errorlevel 1 ( echo ERROR: stage2.asm failed & goto :fail )

echo [3/5] Assembling start.asm...
%NASM% -f elf32 %KERNEL%\start.asm -o %BUILD%\start.o
if errorlevel 1 ( echo ERROR: start.asm failed & goto :fail )

echo [4/5] Compiling C sources...
for %%f in (kernel console pic idt keyboard disk fat12 serial netfs time) do (
    echo     Compiling %%f.c...
    %CC% %CFLAGS% -c %KERNEL%\%%f.c -o %BUILD%\%%f.o
    if errorlevel 1 ( echo ERROR: %%f.c failed & goto :fail )
)

echo [5/5] Linking and assembling image...
%LD% %LDFLAGS% -o %BUILD%\kernel.bin %BUILD%\start.o %BUILD%\kernel.o %BUILD%\console.o %BUILD%\pic.o %BUILD%\idt.o %BUILD%\keyboard.o %BUILD%\disk.o %BUILD%\fat12.o %BUILD%\serial.o %BUILD%\netfs.o %BUILD%\time.o
if errorlevel 1 ( echo ERROR: Linking failed & goto :fail )

%PYTHON% %MKIMAGE%
if errorlevel 1 ( echo ERROR: mkimage.py failed & goto :fail )

echo Rebuild done. Disk data in fat12.img preserved.
goto :end

:: ── run ────────────────────────────────────────────────────────────────────
:run_only
if not exist %BUILD%\myos.img (
    echo ERROR: No disk image found. Run 'build.bat' first.
    goto :fail
)
echo Launching QEMU...
qemu-system-i386 -drive file=%BUILD%\myos.img,format=raw,index=0,media=disk -serial tcp::4444,server,nowait
goto :end

:: ── addfiles ───────────────────────────────────────────────────────────────
:addfiles
if not exist %BUILD%\fat12.img ( echo ERROR: fat12.img not found. Run 'build.bat' first. & goto :fail )
%PYTHON% %ADDFILES%
if errorlevel 1 ( echo ERROR: addfiles.py failed & goto :fail )
echo Re-assembling disk image with updated FAT12...
%PYTHON% %MKIMAGE%
if errorlevel 1 ( echo ERROR: mkimage.py failed & goto :fail )
goto :end

:: ── resetdisk ──────────────────────────────────────────────────────────────
:: DESTRUCTIVE: wipes fat12.img and repopulates with defaults.
:: Use when you want a completely clean disk.
:resetdisk
echo WARNING: This will wipe all files on the virtual disk!
echo Press Ctrl+C to cancel, or any key to continue...
pause > nul
%PYTHON% %MAKEDISK% --reset
if errorlevel 1 ( echo ERROR: makedisk.py --reset failed & goto :fail )
%PYTHON% %ADDFILES%
if errorlevel 1 ( echo ERROR: addfiles.py failed & goto :fail )
echo Re-assembling disk image...
%PYTHON% %MKIMAGE%
if errorlevel 1 ( echo ERROR: mkimage.py failed & goto :fail )
echo Disk reset complete.
goto :end

:: ── diskinfo ───────────────────────────────────────────────────────────────
:diskinfo
if not exist %BUILD%\fat12.img ( echo ERROR: fat12.img not found. Run 'build.bat' first. & goto :fail )
%PYTHON% %MAKEDISK% --status
goto :end

:: ── clean ──────────────────────────────────────────────────────────────────
:: Removes all build outputs. fat12.img is also removed (full clean).
:clean
if exist %BUILD% rmdir /s /q %BUILD%
echo Cleaned. (fat12.img removed — run 'build.bat' to recreate)
goto :end

:fail
echo BUILD FAILED.
exit /b 1

:end
endlocal