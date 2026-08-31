$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$product = Join-Path $root 'firmware\esp32_s3_touch_amoled_1_75\ui_static_mock'
$test = Join-Path $root 'integration_tests\idf_arduino_hybrid_full_spike\host_tests\workout_speed_fallback_test.cpp'
$out = Join-Path $env:TEMP 'workout_speed_fallback_test.exe'
& g++ -std=c++17 -Wall -Wextra -Werror -I $product $test `
    (Join-Path $product 'workout_imu_estimator.cpp') `
    (Join-Path $product 'workout_speed_resolver.cpp') -o $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Workout speed fallback host test: PASS'
