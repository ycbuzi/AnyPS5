# Exports the AnyPS5 source of the OS toolchain worktree, with every submodule at the commit the tree pins, as tar
# files that build-toolchain.sh unpacks inside WSL. Nothing is downloaded: submodules come from the local clones under
# D:\AnyPS5\.git\modules and D:\AnyPS5\.git\worktrees\*\modules; a missing one is an error naming it.
# usage: export-source.ps1 [-Worktree C:\AnyPS5-work\wt-cbint] | [-Worktree "" -Ref origin/main [-Merge <branch>]] [-Fetch]
#   -Worktree  export this worktree's committed HEAD as it is (default: wt-cbint, branch chromebook/integration-1116 =
#              the AVX-to-SSE lowering merged with quest/integration-1116; -Worktree C:\AnyPS5-work\wt-sse exports the
#              lowering alone); it must have no uncommitted changes to tracked files
#   -Ref       with -Worktree "": what the worktree C:\AnyPS5-work\wt-ostoolchain is reset to (detached)
#   -Merge     with -Worktree "": a local branch merged on top; a conflict stops the export
#   -Fetch     git fetch origin first (network: only when Blu allows it)
param([string]$Worktree = "C:\AnyPS5-work\wt-cbint", [string]$Ref = "origin/main", [string]$Merge = "", [switch]$Fetch)
$ErrorActionPreference = "Continue"
$Repo = "D:\AnyPS5"
$Wt = "C:\AnyPS5-work\wt-ostoolchain"
$Out = "C:\AnyPS5-work\ostoolchain-src"
if ($Fetch) { git -C $Repo fetch origin --quiet; if ($LASTEXITCODE) { throw "git fetch failed" } }
$lowering = "no"
if ($Worktree) {
    $Wt = $Worktree
    if (-not (Test-Path -LiteralPath $Wt)) { throw "no worktree $Wt" }
    if (git -C $Wt status --porcelain --untracked-files=no) { throw "$Wt has uncommitted changes; commit them first" }
    $Ref = (git -C $Wt rev-parse --abbrev-ref HEAD).Trim()
    $lowering = $Ref
} elseif (-not (Test-Path -LiteralPath $Wt)) {
    git -C $Repo worktree add --detach $Wt $Ref | Out-Null
    if ($LASTEXITCODE) { throw "git worktree add $Wt failed" }
} else {
    git -C $Wt checkout --detach --force $Ref --quiet
    if ($LASTEXITCODE) { throw "checkout $Ref in $Wt failed" }
}
if (-not $Worktree -and $Merge) {
    git -C $Wt -c user.name=ostoolchain -c user.email=ostoolchain@localhost merge --no-edit --quiet $Merge
    if ($LASTEXITCODE) { git -C $Wt merge --abort; throw "merging $Merge onto $Ref conflicts" }
    $lowering = $Merge
}
$head = (git -C $Wt rev-parse HEAD).Trim()
$date = (git -C $Wt log -1 --format=%cI HEAD).Trim()
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$manifest = [Collections.Generic.List[string]]::new()
$script:n = 0
$script:skipped = [Collections.Generic.List[string]]::new()
function Export-Tree([string]$gitDir, [string]$commit, [string]$path) {
    $script:n++
    $tar = "{0:D3}.tar" -f $script:n
    git --git-dir=$gitDir -c core.autocrlf=false -c core.eol=lf archive --format=tar -o (Join-Path $Out $tar) $commit
    if ($LASTEXITCODE) { throw "git archive $path at $commit failed" }
    $manifest.Add("$tar`t$path")
    if (-not (git --git-dir=$gitDir ls-tree $commit -- .gitmodules)) { return }
    $modules = git --git-dir=$gitDir show "${commit}:.gitmodules"
    if ($LASTEXITCODE -or -not $modules) { return }
    $names = @{}
    $current = $null
    foreach ($line in $modules) {
        if ($line -match '^\s*\[submodule\s+"(.+)"\]') { $current = $Matches[1] }
        elseif ($current -and $line -match '^\s*path\s*=\s*(.+?)\s*$') { $names[$Matches[1]] = $current }
    }
    foreach ($sub in $names.Keys) {
        $entry = git --git-dir=$gitDir ls-tree $commit -- $sub
        if (-not ($entry -match '^160000 commit ([0-9a-f]{40})')) { continue }
        $sha = $Matches[1]
        $name = $names[$sub]
        $candidates = @((Join-Path $gitDir "modules\$name"), (Join-Path $gitDir "modules\$sub"))
        if ($path -eq "") {
            $candidates += @(Get-ChildItem -LiteralPath (Join-Path $Repo ".git\worktrees") -Directory | ForEach-Object { Join-Path $_.FullName "modules\$name" })
        } else {
            $candidates += @(Get-ChildItem -LiteralPath (Join-Path $Repo ".git\worktrees") -Directory | ForEach-Object { Join-Path $_.FullName "modules\$path\modules\$name" })
            $candidates += @(Join-Path $Repo ".git\modules\$path\modules\$name")
        }
        $found = $null
        foreach ($c in ($candidates | Select-Object -Unique)) {
            if (-not (Test-Path -LiteralPath (Join-Path $c "HEAD"))) { continue }
            $type = git --git-dir=$c cat-file -t $sha 2>&1
            if ($LASTEXITCODE -eq 0 -and "$type" -eq "commit") { $found = $c; break }
        }
        $full = if ($path) { "$path/$sub" } else { $sub }
        if (-not $found -and $path) { $script:skipped.Add($full); "  skipped nested $full (no local clone; the build does not use it unless it fails)"; continue }
        if (-not $found) { throw "no local clone has $full at $sha (run export-source.ps1 -Fetch after a submodule update in D:\AnyPS5)" }
        Export-Tree $found $sha $full
    }
}
Export-Tree (Join-Path $Repo ".git") $head ""
[IO.File]::WriteAllLines((Join-Path $Out "manifest.tsv"), $manifest)
[IO.File]::WriteAllLines((Join-Path $Out "source.env"), @("COMMIT=$head", "COMMIT_DATE=$date", "REF=$Ref", "LOWERING=$lowering", "SKIPPED=$($script:skipped -join ' ')"))
"exported $head ($($manifest.Count) trees) to $Out"
