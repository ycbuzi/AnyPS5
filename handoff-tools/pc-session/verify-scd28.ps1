. D:\AnyPS5-tools\env.ps1
Set-Location D:\AnyPS5
$srcFile = 'core/libs/tests/GuestFont.cpp'

function Invoke-GuestFont {
    param([string]$Ref, [string]$Label, [int]$Runs)
    git checkout $Ref -- $srcFile
    $buildOut = cmake --build D:\AnyPS5\build --target guest_font_tests --parallel 2>&1 | Out-String
    if ($buildOut -match 'build stopped|FAILED') {
        Write-Output ("{0,-26} BUILD FAILED - result discarded" -f $Label)
        ($buildOut -split "`n" | Select-String 'error|FAILED' | Select-Object -First 3)
        return
    }
    $passCount = 0; $failCount = 0
    for ($run = 1; $run -le $Runs; $run++) {
        ctest --test-dir D:\AnyPS5\build -R '^guest_font$' --timeout 120 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { $passCount++ } else { $failCount++ }
    }
    Write-Output ("{0,-26} pass={1} fail={2} of {3}" -f $Label, $passCount, $failCount, $Runs)
}

Write-Output "### trigger environment (nothing created or deleted) ###"
Write-Output "ANYPS5_SYSTEM_FONTS = $env:ANYPS5_SYSTEM_FONTS"
Write-Output "font files present  = $((Get-ChildItem $env:ANYPS5_SYSTEM_FONTS -Filter *.tt* | Measure-Object).Count)"

Write-Output "`n### A) CONTROL: upstream's version, expect FAIL ###"
Invoke-GuestFont -Ref 'origin/main' -Label 'upstream origin/main' -Runs 5

Write-Output "`n### B) SCD28's exact commit 6dc37014, expect PASS ###"
Invoke-GuestFont -Ref '6dc37014' -Label 'SCD28 6dc37014' -Runs 8

Write-Output "`n### C) sibling font tests with SCD28's patch applied ###"
ctest --test-dir D:\AnyPS5\build -R 'font' --timeout 180 2>&1 | Select-String 'Test #|tests passed'

Write-Output "`n### restore our tree ###"
git checkout HEAD -- $srcFile
cmake --build D:\AnyPS5\build --target guest_font_tests --parallel 2>&1 | Select-Object -Last 1
$dirty = git status --porcelain
if ($dirty) { Write-Output "  DIRTY: $dirty" } else { Write-Output "  clean" }
