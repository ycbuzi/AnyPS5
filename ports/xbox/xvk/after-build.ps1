$log = "C:\AnyPS5-work\build-gpucap\xvk-build.log"
while (-not (Select-String -Path $log -Pattern "^exit " -Quiet)) { Start-Sleep -Seconds 60 }
$exit = (Select-String -Path $log -Pattern "^exit (\d+)").Matches[0].Groups[1].Value
"build exit $exit"
Select-String -Path $log -Pattern "FAILED:" | Select-Object -First 6 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(200, $_.Line.Length)) }
$relinker = "C:\AnyPS5-work\build-gpucap\core\relinker\relinker.exe"
$agc = "C:\AnyPS5-work\build-gpucap\core\libs\libs\libSceAgcDriver.prx"
if (-not (Test-Path $relinker) -or -not (Test-Path $agc) -or (Get-Item $agc).LastWriteTime -lt (Get-Date).AddHours(-12)) { "missing or stale outputs"; exit 1 }
$env:PATH = "D:\AnyPS5-tools\mingw-extract\mingw64\bin;" + $env:PATH
& "C:\AnyPS5 Games\_tools\build-game.ps1" -GameDir "C:\AnyPS5-work\xvk-celeste" 2>&1 | Select-Object -Last 4
& "C:\AnyPS5-work\xbox-probe\xvk\run-celeste.ps1" -Seconds 40
