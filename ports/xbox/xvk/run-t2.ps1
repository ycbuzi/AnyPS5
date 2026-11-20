$here = Split-Path -Parent $MyInvocation.MyCommand.Path
& cmd /c "$here\build.cmd" 2>&1 | Select-String "error|built" | Select-Object -First 10 | ForEach-Object { $_.Line }
$env:XVK_LOG = "$here\t2\xvk.log"
$env:XVK_SHADERS = "$here\t2\cache"
Remove-Item $env:XVK_LOG -ErrorAction SilentlyContinue
& "$here\bin\xvktest2.exe" "$here\bin\xvk.dll"
"--- xvk.log"
Get-Content $env:XVK_LOG -ErrorAction SilentlyContinue | Group-Object | Sort-Object Count -Descending | Select-Object -First 12 | ForEach-Object { "$($_.Count)x $($_.Name)" }
