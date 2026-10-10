# Build the rebased stack and run the full suite.
$ErrorActionPreference = 'Continue'
. D:\AnyPS5-tools\env.ps1
Set-Location D:\AnyPS5

$t0 = Get-Date
"HEAD: $(git rev-parse --short HEAD)  ahead of origin/main: $(git rev-list --count origin/main..HEAD)"

"`n########## CONFIGURE ##########"
cmake -S D:\AnyPS5 -B D:\AnyPS5\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON 2>&1 | Select-Object -Last 6
"configure exit=$LASTEXITCODE"

"`n########## BUILD (all) ##########"
cmake --build D:\AnyPS5\build --parallel 2>&1 | Select-Object -Last 25
"build exit=$LASTEXITCODE elapsed=$([math]::Round(((Get-Date)-$t0).TotalMinutes,1))min"

"`n########## BUILD (libs) ##########"
cmake --build D:\AnyPS5\build --target libs --parallel 2>&1 | Select-Object -Last 12
"libs exit=$LASTEXITCODE"

"`n########## ISSUE 2535: check_test_registration.py ##########"
python D:\AnyPS5\tools\check_test_registration.py D:\AnyPS5\build 2>&1 | Select-Object -Last 12
"check_test_registration exit=$LASTEXITCODE  (nonzero reproduces issue #2535)"

"`n########## FULL SUITE ##########"
ctest --test-dir D:\AnyPS5\build --output-on-failure --timeout 300 2>&1 | Select-Object -Last 45
"ctest exit=$LASTEXITCODE"

"`nTOTAL=$([math]::Round(((Get-Date)-$t0).TotalMinutes,1))min"
