. D:\AnyPS5-tools\env.ps1 | Out-Null
$cache = "D:\AnyPS5-tools\shadercache-fresh-$(Get-Random)"
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$env:ANYPS5_SHADER_CACHE_DIR = $cache
Write-Output "fresh shader cache: $cache"
Set-Location D:\AnyPS5
cmake --build build 2>&1 | Select-String -Pattern "error|FAILED|undefined reference" | ForEach-Object { $_.Line }
Write-Output "=== build done, running suite ==="
Set-Location D:\AnyPS5\build
ctest -j 8 2>&1 | Select-Object -Last 60
