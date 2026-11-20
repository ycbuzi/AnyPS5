$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Sdk = "C:\Program Files (x86)\Windows Kits\10"
$Ver = "10.0.26100.0"
$Bin = "$Sdk\bin\$Ver\x64"
$Out = Join-Path $Here "out"
$Layout = Join-Path $Out "layout"
$Gen = Join-Path $Out "gen"
New-Item -ItemType Directory -Force $Layout, "$Layout\Assets" | Out-Null

if (-not (Test-Path "$Gen\winrt\Windows.System.h")) {
    & "$Bin\cppwinrt.exe" -in "$Sdk\UnionMetadata\$Ver\Windows.winmd" -out $Gen
    if ($LASTEXITCODE) { throw "cppwinrt failed" }
}

python (Join-Path $Here "assets.py") "$Layout\Assets"
if ($LASTEXITCODE) { throw "assets failed" }

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$msvc = (Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" | Sort-Object Name | Select-Object -Last 1).FullName
$cmd = "`"$vcvars`" >nul && cd /d `"$Out`" && cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /DWINAPI_FAMILY=WINAPI_FAMILY_APP /DUNICODE /D_UNICODE " +
       "/I`"$Gen`" `"$Here\probe.cpp`" /Fe:`"$Layout\probe.exe`" /link /APPCONTAINER /SUBSYSTEM:WINDOWS " +
       "WindowsApp.lib d3d12.lib dxgi.lib"
$ErrorActionPreference = "Continue"
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
cmd /c "$cmd 2>&1"
if ($LASTEXITCODE) { throw "compile failed" }
$ErrorActionPreference = "Stop"

Copy-Item (Join-Path $Here "AppxManifest.xml") $Layout -Force
Remove-Item "$Layout\dxbc" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$Layout\dxbc" | Out-Null
foreach ($set in @("all", "celeste", "xvk-celeste")) {
    $Dxbc = Join-Path $Here "hlsl51\runs\$set\dxbc"
    if (Test-Path $Dxbc) { Copy-Item "$Dxbc\*.dxbc", "$Dxbc\*.gfx" "$Layout\dxbc" -ErrorAction SilentlyContinue }
}
$priConfig = Join-Path $Out "priconfig.xml"
& "$Bin\makepri.exe" createconfig /cf $priConfig /dq en-US /o | Out-Null
if ($LASTEXITCODE) { throw "makepri createconfig failed" }
& "$Bin\makepri.exe" new /pr $Layout /cf $priConfig /mn (Join-Path $Layout "AppxManifest.xml") /of (Join-Path $Layout "resources.pri") /o | Out-Null
if ($LASTEXITCODE) { throw "makepri new failed" }
$pkg = Join-Path $Out "AnyPS5Probe.appx"
Remove-Item $pkg -ErrorAction SilentlyContinue
& "$Bin\makeappx.exe" pack /o /d $Layout /p $pkg | Out-Null
if ($LASTEXITCODE) { throw "makeappx failed" }

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=AnyPS5 Test" -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=AnyPS5 Test" -KeyUsage DigitalSignature -FriendlyName "AnyPS5 test package signing" `
        -CertStoreLocation Cert:\CurrentUser\My -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
}
& "$Bin\signtool.exe" sign /q /fd SHA256 /sha1 $cert.Thumbprint $pkg
if ($LASTEXITCODE) { throw "signtool failed" }
Export-Certificate -Cert $cert -FilePath (Join-Path $Out "AnyPS5Test.cer") | Out-Null
Write-Output "built $pkg"
