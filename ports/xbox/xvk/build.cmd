@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist bin mkdir bin
set INC=/I"D:\AnyPS5\3rdparty\Vulkan-Headers\include" /I..\3rdparty\SPIRV-Cross /I"D:\AnyPS5\3rdparty\SPIRV-Tools\include"
if not exist bin\spirv-cross-hlsl.lib call build-cross.cmd || exit /b 1
if not exist bin\spvtools\source\SPIRV-Tools.lib call build-spvtools.cmd || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /W3 /MT %INC% xvk.cpp /LD /Fe:bin\xvk.dll /Fo:bin\ /link /MAP:bin\xvk.map bin\spirv-cross-hlsl.lib bin\spvtools\source\SPIRV-Tools.lib d3d12.lib dxgi.lib d3dcompiler.lib user32.lib || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /W3 /MT %INC% xvktest.cpp /Fe:bin\xvktest.exe /Fo:bin\ /link user32.lib || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /W3 /MT %INC% xvktest2.cpp /Fe:bin\xvktest2.exe /Fo:bin\ /link user32.lib || exit /b 1
echo built
