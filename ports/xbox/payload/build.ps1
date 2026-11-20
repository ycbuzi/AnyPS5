$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Out = Join-Path $Here "out"
New-Item -ItemType Directory -Force $Out | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$cmd = "`"$vcvars`" >nul && cd /d `"$Out`" && cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /LD /D_CRT_SECURE_NO_WARNINGS `"$Here\payload.cpp`" /Fe:`"$Out\payload.dll`" /link /DYNAMICBASE d3d12.lib d3dcompiler.lib kernel32.lib"
$ErrorActionPreference = "Continue"
cmd /c "$cmd 2>&1"
if ($LASTEXITCODE) { throw "payload compile failed" }
Write-Output "built $Out\payload.dll"
