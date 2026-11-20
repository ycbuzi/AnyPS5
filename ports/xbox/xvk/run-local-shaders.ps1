param([string]$GameDir = "C:\AnyPS5-work\xvk-celeste", [string]$Exe = "Celeste Classic.exe", [int]$Seconds = 30, [switch]$Python)
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$cache = Join-Path $env:TEMP ("xvk-local-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $cache | Out-Null
Copy-Item "$here\bin\xvk.dll" "$GameDir\vulkan-1.dll" -Force
$env:XVK_LOG = "$GameDir\xvk.log"
if ($Python) { Remove-Item Env:XVK_LOCAL_SHADERS -ErrorAction SilentlyContinue } else { $env:XVK_LOCAL_SHADERS = "1" }
$env:XVK_SHADERS = $cache
$env:XVK_INT64 = "1"
$env:ANYPS5_SINGLE_LANE_OWN_SELECTOR = "1"
Remove-Item "$GameDir\xvk.log", "$GameDir\anyps5.log", "$GameDir\xvk_frame_*.bmp" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$GameDir\$Exe" -WorkingDirectory $GameDir -PassThru -WindowStyle Minimized
if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force; "running after ${Seconds}s (killed)" } else { "exit $($p.ExitCode)" }
$dirs = @(Get-ChildItem $cache -Directory)
$ok = @($dirs | Where-Object { Test-Path (Join-Path $_.FullName ($_.Name + ".dxbc")) })
"shaders converted ($(if ($Python) { "python" } else { "on device" })): $($ok.Count) of $($dirs.Count)"
$dirs | Where-Object { $ok -notcontains $_ } | ForEach-Object { "no dxbc: $($_.Name) [" + ((Get-ChildItem $_.FullName).Name -join " ") + "]" }
"--- xvk.log"
Get-Content "$GameDir\xvk.log" -ErrorAction SilentlyContinue | Group-Object | Sort-Object Count -Descending | Select-Object -First 12 | ForEach-Object { "$($_.Count)x $($_.Name)" }
"frames: " + (@(Get-ChildItem "$GameDir\xvk_frame_*.bmp" -ErrorAction SilentlyContinue).Name -join " ")
Remove-Item $cache -Recurse -Force
