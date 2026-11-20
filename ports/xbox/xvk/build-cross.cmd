@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set SC=..\3rdparty\SPIRV-Cross
if not exist bin\cross mkdir bin\cross
cl /nologo /O2 /EHsc /std:c++17 /W0 /MT /MP /c %SC%\spirv_cross.cpp %SC%\spirv_parser.cpp %SC%\spirv_cross_parsed_ir.cpp %SC%\spirv_cfg.cpp %SC%\spirv_glsl.cpp %SC%\spirv_hlsl.cpp /Fo:bin\cross\ || exit /b 1
lib /nologo /OUT:bin\spirv-cross-hlsl.lib bin\cross\*.obj || exit /b 1
echo built
