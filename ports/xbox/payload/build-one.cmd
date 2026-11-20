@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0out"
cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /LD /D_CRT_SECURE_NO_WARNINGS "%~dp0%1.cpp" /Fe:"%~dp0out\%1.dll" /link /DYNAMICBASE d3d12.lib kernel32.lib advapi32.lib runtimeobject.lib
