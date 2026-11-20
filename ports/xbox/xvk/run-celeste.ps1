param([int]$Seconds = 25)
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$d = "C:\AnyPS5-work\xvk-celeste"
$b = & cmd /c "$here\build.cmd" 2>&1 | Select-String -Pattern "error|built"
$b | Select-Object -First 8 | ForEach-Object { $_.Line }
if (-not ($b -match "built")) { exit 1 }
Copy-Item "$here\bin\xvk.dll" "$d\vulkan-1.dll" -Force
$env:XVK_LOG = "$d\xvk.log"
Remove-Item "$d\xvk.log", "$d\anyps5.log" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$d\Celeste Classic.exe" -WorkingDirectory $d -PassThru -WindowStyle Minimized
if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force; "running after ${Seconds}s (killed)" } else { "exit $($p.ExitCode)" }
"--- xvk.log"
Get-Content "$d\xvk.log" -ErrorAction SilentlyContinue | Group-Object | Sort-Object Count -Descending | Select-Object -First 15 | ForEach-Object { "$($_.Count)x $($_.Name)" }
"--- anyps5.log"
Select-String -Path "$d\anyps5.log" -Pattern "VulkanDevice|what\(\)|FATAL|requires|missing|unsupported|flip|Flip" -ErrorAction SilentlyContinue | Select-Object -Last 12 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(230, $_.Line.Length)) }
