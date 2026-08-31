$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot "../..")).Path
$productDir = Join-Path $repositoryRoot "firmware/esp32_s3_touch_amoled_1_75/ui_static_mock"
$testBinary = Join-Path $env:TEMP "ig_workout_calories_test.exe"

& g++ -std=c++17 -Wall -Wextra -Werror `
    -I $productDir `
    (Join-Path $projectRoot "host_tests/workout_calories_test.cpp") `
    (Join-Path $productDir "workout_calories.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "WORKOUT_CALORIES_TEST: PASS cases=MET_BOUNDARIES,10_MINUTES,PAUSE,RESUME,SPEED_CHANGE"
