# Runs build-toolchain.sh in WSL Arch while holding the PC lock (gpu_flag.ps1 Use-Pc, priority 50 = manual run).
# The lock is refreshed every minute so a long build is not taken over; the log is C:\AnyPS5-work\ostoolchain-build.log.
# usage: powershell -NoProfile -File run-locked.ps1 [-Jobs 12]
param([int]$Jobs = 12)
. "D:\AnyPS5 Games\_tools\assistant\skills\gpu_flag.ps1"
$log = "C:\AnyPS5-work\ostoolchain-build.log"
$script = "/mnt/c/AnyPS5 Games/_tools/anyps5-os-stick/toolchain/build-toolchain.sh"
[IO.File]::WriteAllText($log, "waiting for the PC lock $(Get-Date -Format 'HH:mm:ss')`n")
$code = 1
Use-Pc "AnyPS5 OS toolchain build (WSL)" 50 {
    Add-Content -LiteralPath $log "PC lock taken $(Get-Date -Format 'HH:mm:ss')"
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = "wsl.exe"
    $psi.Arguments = "-d archlinux -u root -- bash -c `"bash '$script' $Jobs >> /mnt/c/AnyPS5-work/ostoolchain-build.log 2>&1`""
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $p = [Diagnostics.Process]::Start($psi)
    while (-not $p.WaitForExit(60000)) { Touch-GpuFlag }
    $script:code = $p.ExitCode
}
Add-Content -LiteralPath $log "exit $code $(Get-Date -Format 'HH:mm:ss')"
exit $code
