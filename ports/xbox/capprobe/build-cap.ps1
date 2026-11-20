$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Bin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$Out = Join-Path $Root "out"
$Layout = Join-Path $Out "caplayout"
python (Join-Path $Here "make_layout.py") @args
if ($LASTEXITCODE) { throw "layout failed" }
$priConfig = Join-Path $Out "cap-priconfig.xml"
& "$Bin\makepri.exe" createconfig /cf $priConfig /dq en-US /o | Out-Null
if ($LASTEXITCODE) { throw "makepri createconfig failed" }
& "$Bin\makepri.exe" new /pr $Layout /cf $priConfig /mn (Join-Path $Layout "AppxManifest.xml") /of (Join-Path $Layout "resources.pri") /o | Out-Null
if ($LASTEXITCODE) { throw "makepri new failed" }
$pkg = Join-Path $Out "AnyPS5CapProbe.appx"
Remove-Item $pkg -ErrorAction SilentlyContinue
& "$Bin\makeappx.exe" pack /o /d $Layout /p $pkg
if ($LASTEXITCODE) { throw "makeappx failed" }
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=AnyPS5 Test" -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) { throw "no AnyPS5 Test certificate" }
& "$Bin\signtool.exe" sign /q /fd SHA256 /sha1 $cert.Thumbprint $pkg
if ($LASTEXITCODE) { throw "signtool failed" }
Write-Output "built $pkg"
