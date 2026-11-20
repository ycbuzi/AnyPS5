@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist bin\spirv-cross-hlsl.lib call build-cross.cmd || exit /b 1
if not exist bin\spvtools\source\SPIRV-Tools.lib call build-spvtools.cmd || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /W3 /MT /I..\3rdparty\SPIRV-Cross /I"D:\AnyPS5\3rdparty\SPIRV-Tools\include" xcrosstest.cpp /Fe:bin\xcrosstest.exe /Fo:bin\ /link bin\spirv-cross-hlsl.lib bin\spvtools\source\SPIRV-Tools.lib d3dcompiler.lib || exit /b 1
echo built
