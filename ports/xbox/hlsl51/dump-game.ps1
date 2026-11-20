param([Parameter(Mandatory)][string]$Game, [int]$Seconds = 120, [string]$Build = "C:\AnyPS5 Games\_tools\anyps5-9d2e737432")
$ErrorActionPreference = "Stop"
. "C:\AnyPS5 Games\_tools\assistant\skills\gpu_flag.ps1"
$src = Join-Path "D:\AnyPS5 Games" $Game
$dst = Join-Path "C:\AnyPS5-work\xbox-probe\dumps" $Game
New-Item -ItemType Directory -Force $dst | Out-Null
$cfg = Get-Content (Join-Path $src "game.json") -Raw | ConvertFrom-Json
$cfg | Add-Member -NotePropertyName anyps5Build -NotePropertyValue $Build -Force
$cfg | ConvertTo-Json | Out-File -Encoding utf8 (Join-Path $dst "game.json")
Use-Pc "xbox shader dump $Game" 40 {
    & "C:\AnyPS5 Games\_tools\build-game.ps1" -GameDir $dst | Select-Object -Last 3
    $exe = Get-ChildItem -LiteralPath $dst -Filter *.exe | Select-Object -First 1
    Get-ChildItem -LiteralPath $dst -Filter *.req -ErrorAction SilentlyContinue | Remove-Item -Force
    $env:APS5_DUMP_SHADERS = "1"
    $env:APS5_DUMP_GRAPHICS_SHADERS = "1"
    $p = & "C:\AnyPS5 Games\_tools\start-game.ps1" -Exe $exe.FullName -Dir $dst
    Start-Sleep $Seconds
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    $n = @(Get-ChildItem -LiteralPath $dst -Filter *.req).Count
    "dumped $n requests into $dst (exit $($p.HasExited))"
} -Game
