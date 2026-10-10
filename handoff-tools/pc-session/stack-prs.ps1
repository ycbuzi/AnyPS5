# Stack as many open function-implementing AnyPS5 PRs as will cleanly apply onto a local branch,
# then measure the completion delta. Conflicting PRs are skipped, not forced.

$ErrorActionPreference = 'Continue'
Set-Location D:\AnyPS5
. D:\AnyPS5-tools\env.ps1 | Out-Null

# #1299 clashes with already-merged #2077; #545/#407/#1694 overlap on libc NID exports (issue #2400).
$prs = @(2477,2129,2090,2208,1097,2123,2028,2478,926,925,571,570,560,572,959,1060,1693,1633,
         1860,1777,1772,1781,1783,1611,1141,1482,2152,2204,1485,1492,1525,1529,1611,1686,
         1776,1909,2034,757,758,1091,1431,1491,1499,1693)
$prs = $prs | Sort-Object -Unique

git checkout main --quiet
git branch -D stack/open-prs --force 2>$null | Out-Null
git checkout -b stack/open-prs --quiet

# keep the videoout build workaround so the tree builds on Windows at all
git add core/libs/prx/libSceVideoOut/src/Event.cpp core/libs/prx/libSceVideoOut/src/Output.cpp 2>$null
git -c user.name=blu -c user.email=131330772+ycbuzi@users.noreply.github.com commit -q -m "local: drop duplicate videoout aliases (#2462)" 2>$null

$applied = @(); $skipped = @()
foreach ($n in $prs) {
    git fetch origin "pull/$n/head" --quiet 2>$null
    if ($LASTEXITCODE -ne 0) { $skipped += "$n (fetch failed)"; continue }
    $head = (git rev-parse FETCH_HEAD 2>$null)
    $base = (git merge-base HEAD FETCH_HEAD 2>$null)
    if (-not $head -or -not $base) { $skipped += "$n (no base)"; continue }
    $count = (git rev-list --count "$base..$head" 2>$null)
    if ([int]$count -eq 0 -or [int]$count -gt 40) { $skipped += "$n ($count commits)"; continue }
    git cherry-pick --allow-empty "$base..$head" 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $applied += "$n (+$count)"
        Write-Host ("  applied #{0}  ({1} commit(s))" -f $n, $count)
    } else {
        git cherry-pick --abort 2>$null | Out-Null
        git reset --hard HEAD --quiet 2>$null
        $skipped += "$n (conflict)"
        Write-Host ("  SKIP    #{0}  conflict" -f $n)
    }
}

Write-Host ""
Write-Host "applied: $($applied.Count)"
$applied | ForEach-Object { Write-Host "    $_" }
Write-Host "skipped: $($skipped.Count)"
$skipped | ForEach-Object { Write-Host "    $_" }

Write-Host ""
Write-Host "================ COMPLETION AFTER STACKING"
$sp = 'C:\Users\Blu66\AppData\Local\Temp\claude\C--\4d4fd5e2-923f-4f26-b6f6-ee6d1f584e98\scratchpad\prog-stacked'
New-Item -ItemType Directory -Force $sp | Out-Null
python D:\AnyPS5\tools\progress.py --root D:\AnyPS5 $sp 2>&1 | Select-Object -First 3
Write-Host "baseline was: libraries 2664/3109 (85.69%)"
