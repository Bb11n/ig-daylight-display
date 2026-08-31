$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot "../..")).Path
$productDir = Join-Path $repositoryRoot "firmware/esp32_s3_touch_amoled_1_75/ui_static_mock"
$testSource = Join-Path $projectRoot "host_tests/system_power_state_test.cpp"
$testBinary = Join-Path $env:TEMP "ig_system_power_state_test.exe"
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Pattern([string]$path, [string]$pattern, [string]$description) {
    $content = Get-Content -LiteralPath $path -Raw
    if (-not [regex]::IsMatch($content, $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $script:failures.Add("missing: $description")
    }
}

$sketch = Join-Path $productDir "ui_static_mock.ino"
$runtime = Join-Path $projectRoot "main/display_runtime.cpp"
Require-Pattern $sketch 'displayFlush[\s\S]*systemPowerAllowsBusinessWork' "LVGL flush SoftOff gate"
Require-Pattern $sketch 'void loop\(\)[\s\S]*powerManagerLoop[\s\S]*systemPowerAllowsBusinessWork' "GPS/UI/LVGL business gate"
Require-Pattern $runtime 'display_runtime_sketch_loop\(\);[\s\S]*systemPowerAllowsBusinessWork\(\)[\s\S]*return;' "BLE and Voice dispatch gate"
Require-Pattern $sketch 'gfx->displayOff\(\)' "CO5300 display-off call"

if ($failures.Count -gt 0) {
    Write-Host "SYSTEM_POWER_STATIC_VERIFY: FAIL ($($failures.Count))"
    $failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}

& g++ -std=c++17 -Wall -Wextra -Werror `
    -I $productDir `
    $testSource `
    (Join-Path $productDir "system_power_state.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "SYSTEM_POWER_STATIC_VERIFY: PASS gates=4"
