$ErrorActionPreference = "Continue"
$src = "C:\AnyPS5-work\wt-sse"
$out = "C:\AnyPS5-work\build-sse-win"
$env:PATH = "D:\AnyPS5-tools\mingw-extract\mingw64\bin;$env:PATH"
Get-ChildItem "$src\3rdparty" -Directory | ForEach-Object {
    $main = Join-Path "D:\AnyPS5\3rdparty" $_.Name
    if (-not (Get-ChildItem $_.FullName -Force | Select-Object -First 1) -and (Test-Path $main)) {
        Remove-Item $_.FullName -Force
        cmd /c mklink /J "$($_.FullName)" "$main" | Out-Null
        "linked $($_.Name)"
    }
}
cmake -G Ninja -S $src -B $out -DCMAKE_BUILD_TYPE=Release -DANYPS5_RELINKER_ONLY=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=c++ 2>&1 | Select-Object -Last 5
ninja -C $out relinker 2>&1 | Select-Object -Last 8
Get-ChildItem "$out\core\relinker\relinker.exe" -ErrorAction SilentlyContinue | Select-Object FullName, Length, LastWriteTime
