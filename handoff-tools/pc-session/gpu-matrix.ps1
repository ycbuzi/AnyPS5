# Run the GPU-dependent AnyPS5 tests on every Vulkan device in this machine.
# Test names are read from libSceAgcDriver's CMakeLists so the set stays correct as it changes.

. D:\AnyPS5-tools\env.ps1 | Out-Null

$cmake = 'D:\AnyPS5\core\libs\prx\libSceAgcDriver\CMakeLists.txt'
$names = Select-String -Path $cmake -Pattern 'add_test\(NAME\s+([A-Za-z0-9_]+)' |
         ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique
Write-Host "GPU-dependent tests discovered: $($names.Count)"
$pattern = '^(' + ($names -join '|') + ')$'

$devices = [ordered]@{
    '4070 SUPER (Ada)'       = '4070'
    '1650 SUPER (Turing)'    = '1650'
    'Radeon iGPU (RDNA2)'    = 'Radeon'
}

$results = @{}

foreach ($label in $devices.Keys) {
    $sel = $devices[$label]
    Write-Host ""
    Write-Host "================ $label   (ANYPS5_GPU=$sel)"
    $env:ANYPS5_GPU = $sel
    $raw = ctest --test-dir D:\AnyPS5\build -R $pattern --output-on-failure 2>&1 | Out-String
    $summary = ($raw -split "`n" | Where-Object { $_ -match 'tests passed|tests failed' }) -join ' | '
    Write-Host "  $summary"
    $failed = [regex]::Matches($raw, '(?m)^\s*\d+\s+-\s+(\S+)\s+\(Failed\)') | ForEach-Object { $_.Groups[1].Value }
    $notrun = [regex]::Matches($raw, '(?m)^\s*\d+\s+-\s+(\S+)\s+\(Not Run\)') | ForEach-Object { $_.Groups[1].Value }
    if ($failed) { Write-Host "  FAILED:"; $failed | ForEach-Object { Write-Host "    $_" } }
    if ($notrun) { Write-Host "  NOT RUN:"; $notrun | ForEach-Object { Write-Host "    $_" } }
    if (-not $failed -and -not $notrun) { Write-Host "  all green" }
    $results[$label] = @{ Summary = $summary; Failed = $failed; NotRun = $notrun }
    $raw | Out-File -Encoding utf8 "C:\Users\Blu66\AppData\Local\Temp\claude\C--\4d4fd5e2-923f-4f26-b6f6-ee6d1f584e98\scratchpad\gpu-$sel.log"
}

Write-Host ""
Write-Host "================ CROSS-GPU COMPARISON"
$all = @()
foreach ($label in $devices.Keys) { $all += $results[$label].Failed }
$all = $all | Sort-Object -Unique
if (-not $all) {
    Write-Host "No failures on any device."
} else {
    Write-Host ("{0,-42} {1,-8} {2,-8} {3,-8}" -f 'test', '4070', '1650', 'Radeon')
    foreach ($t in $all) {
        $row = foreach ($label in $devices.Keys) {
            if ($results[$label].Failed -contains $t) { 'FAIL' }
            elseif ($results[$label].NotRun -contains $t) { 'notrun' }
            else { 'pass' }
        }
        Write-Host ("{0,-42} {1,-8} {2,-8} {3,-8}" -f $t, $row[0], $row[1], $row[2])
    }
}
Write-Host ""
Write-Host "per-device logs written to the scratchpad as gpu-<sel>.log"
