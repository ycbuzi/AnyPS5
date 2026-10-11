# Audit where our harvested work came from, and whether upstream merged or rejected it.
#
# Upstream merges a function only when a real title stops on it with a log line from main
# (their own closing comments say so: PR #1993, #1868). Work harvested from REJECTED PRs is
# therefore the work most likely to keep conflicting with upstream's tests at every sync --
# which it did three times on 2026-10-11. This tells us which commits those are.

Set-Location D:\AnyPS5
$out = "D:\AnyPS5\scratchpad\provenance.json"
# [char]31 , not `u{001f} -- this is Windows PowerShell 5.1, where the `u{} escape
# does not exist and the split silently yields one field, matching nothing.
$sep = [char]31
$subjects = git log --no-merges --format="%H$sep%an$sep%s" origin/main..HEAD |
    ForEach-Object { $p = $_ -split $sep; [pscustomobject]@{ sha = $p[0]; author = $p[1]; subject = $p[2] } } |
    Where-Object { $_.subject -match "^(feat|fix)\(" }
Write-Output "distinct commits to check: $($subjects.Count)"

# one row per distinct subject; several of our commits can share one upstream PR
$seen = @{}
$rows = @()
$i = 0
foreach ($c in $subjects) {
    if ($seen.ContainsKey($c.subject)) { continue }
    $seen[$c.subject] = $true
    $i++
    $q = $c.subject -replace '[^\w ]', ' '
    $pr = $null; $state = "no-match"; $num = $null
    try {
        $hit = gh search prs --repo boykopovar/AnyPS5 --match title $q --limit 1 --json number 2>$null | ConvertFrom-Json
        if ($hit) {
            $num = $hit[0].number
            $v = gh pr view $num --repo boykopovar/AnyPS5 --json mergedAt,state 2>$null | ConvertFrom-Json
            $state = if ($v.mergedAt) { "merged" } elseif ($v.state -eq "OPEN") { "open" } else { "rejected" }
        }
    } catch { $state = "lookup-failed" }
    $rows += [pscustomobject]@{ sha = $c.sha.Substring(0, 9); author = $c.author; subject = $c.subject; pr = $num; state = $state }
    Write-Output ("{0,3}/{1}  {2,-9} #{3,-5} {4}" -f $i, $seen.Count, $state, $num, $c.subject)
}

$rows | ConvertTo-Json -Depth 3 | Set-Content $out -Encoding utf8
Write-Output ""
Write-Output "=== SUMMARY ==="
$rows | Group-Object state | Sort-Object Count -Descending | ForEach-Object { "  {0,-14} {1}" -f $_.Name, $_.Count }
Write-Output ""
Write-Output "=== REJECTED (these fight us every sync) ==="
$rows | Where-Object { $_.state -eq "rejected" } | ForEach-Object { "  #{0,-5} {1}" -f $_.pr, $_.subject }
Write-Output ""
Write-Output "wrote $out"
