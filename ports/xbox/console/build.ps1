$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Sdk = "C:\Program Files (x86)\Windows Kits\10"
$Bin = "$Sdk\bin\10.0.26100.0\x64"
$Out = Join-Path $Here "out"
$Layout = Join-Path $Out "layout"
New-Item -ItemType Directory -Force $Out | Out-Null
python (Join-Path $Here "stage.py") $Layout
if ($LASTEXITCODE) { throw "stage failed" }
$Gen = Join-Path (Split-Path -Parent $Here) "out\gen"
$GameObj = Join-Path $Out "gameobj"
New-Item -ItemType Directory -Force $GameObj | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$cmd = "`"$vcvars`" >nul && cd /d `"$GameObj`" && cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /DWINAPI_FAMILY=WINAPI_FAMILY_APP /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS " +
       "/I`"$Gen`" `"$Here\game\anyps5game.cpp`" /Fe:`"$Layout\anyps5game.exe`" /link /APPCONTAINER /SUBSYSTEM:WINDOWS WindowsApp.lib"
$ErrorActionPreference = "Continue"
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
cmd /c "$cmd 2>&1"
if ($LASTEXITCODE) { throw "game part compile failed" }
$ErrorActionPreference = "Stop"
$priConfig = Join-Path $Out "priconfig.xml"
& "$Bin\makepri.exe" createconfig /cf $priConfig /dq en-US /o | Out-Null
if ($LASTEXITCODE) { throw "makepri createconfig failed" }
& "$Bin\makepri.exe" new /pr $Layout /cf $priConfig /mn (Join-Path $Layout "AppxManifest.xml") /of (Join-Path $Layout "resources.pri") /o | Out-Null
if ($LASTEXITCODE) { throw "makepri new failed" }
$pkg = Join-Path $Out "AnyPS5Console.appx"
Remove-Item $pkg -ErrorAction SilentlyContinue
& "$Bin\makeappx.exe" pack /o /d $Layout /p $pkg | Out-Null
if ($LASTEXITCODE) { throw "makeappx failed" }
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=AnyPS5 Test" -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) { throw "no AnyPS5 Test certificate; run the probe build once" }
& "$Bin\signtool.exe" sign /q /fd SHA256 /sha1 $cert.Thumbprint $pkg
if ($LASTEXITCODE) { throw "signtool failed" }
Write-Output "built $pkg"
