$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$product = Join-Path $root 'firmware\esp32_s3_touch_amoled_1_75\ui_static_mock'
$ino = Get-Content (Join-Path $product 'ui_static_mock.ino') -Raw
$cmake = Get-Content (Join-Path $root 'integration_tests\idf_arduino_hybrid_full_spike\main\CMakeLists.txt') -Raw
$imu = Get-Content (Join-Path $product 'workout_imu_speed.cpp') -Raw

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Require ($ino -match 'workoutSpeedResolver\.update') 'Unified speed resolver is not connected.'
Require ($ino -match 'workoutCaloriesUpdate\(effectiveWorkoutSpeedKmh') 'Calories must use effective speed.'
Require ($ino -match 'displayStateUpdateWorkout\([\s\S]*?effectiveWorkoutSpeedKmh') 'Display State must use effective speed.'
Require ($ino -match 'WORKOUT_SPEED_IMU_EST') 'IMU distance fallback is not connected.'
Require ($cmake -match 'workout_imu_speed\.cpp') 'QMI8658 adapter is missing from the build.'
Require ($cmake -match 'workout_speed_resolver\.cpp') 'Speed resolver is missing from the build.'
Require ($imu -notmatch 'Wire\.begin|i2c_driver_install|i2c_master_cmd_begin') 'IMU adapter must reuse Shared Wire NG.'
Require ($imu -notmatch 'xTaskCreate|esp_timer_create') 'IMU adapter must not create a task or timer.'
Write-Host 'WORKOUT_IMU_INTEGRATION_VERIFY: PASS'
