@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set VCPKG_ROOT=P:\Development\personal\games\NextGenGameEngine\vcpkg
del build\CMakeCache.txt 2>nul
cmake --preset windows-debug
