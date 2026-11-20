$ErrorActionPreference = "Stop"
$Bin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$L = Join-Path $Here "layout"; $pkg = Join-Path $Here "AnyPS5Hello.appx"
& "$Bin\makepri.exe" createconfig /cf "$Here\pri.xml" /dq en-US /o | Out-Null
& "$Bin\makepri.exe" new /pr $L /cf "$Here\pri.xml" /mn "$L\AppxManifest.xml" /of "$L\resources.pri" /o | Out-Null
Remove-Item $pkg -ErrorAction SilentlyContinue
& "$Bin\makeappx.exe" pack /o /d $L /p $pkg | Out-Null
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=AnyPS5 Test" } | Select-Object -First 1
& "$Bin\signtool.exe" sign /q /fd SHA256 /sha1 $cert.Thumbprint $pkg
"ok"
