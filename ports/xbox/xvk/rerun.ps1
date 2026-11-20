param([string[]]$Targets = @("libSceAgcDriver"))
$env:PATH = "D:\AnyPS5-tools\mingw-extract\mingw64\bin;" + $env:PATH
Push-Location C:\AnyPS5-work\build-gpucap
$out = & cmake --build . -j 10 --target @Targets 2>&1
$code = $LASTEXITCODE
Pop-Location
$out | Select-String "error|FAILED" | Select-Object -First 8 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
"build exit $code"
if ($code -ne 0) { exit 1 }
& "C:\AnyPS5 Games\_tools\build-game.ps1" -GameDir "C:\AnyPS5-work\xvk-celeste" 2>&1 | Select-Object -Last 1
& "C:\AnyPS5-work\xbox-probe\xvk\run-celeste.ps1" -Seconds 40
