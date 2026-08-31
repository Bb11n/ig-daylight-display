$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot "../..")).Path
$productDir = Join-Path $repositoryRoot "firmware/esp32_s3_touch_amoled_1_75/ui_static_mock"
$hostTestDir = Join-Path $projectRoot "host_tests"
$testBinary = Join-Path $env:TEMP "ig_voice_command_adapter_test.exe"
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Pattern([string]$relativePath, [string]$pattern, [string]$description) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $script:failures.Add("missing file: $relativePath")
        return
    }
    $content = Get-Content -LiteralPath $path -Raw
    if (-not [regex]::IsMatch($content, $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $script:failures.Add("$relativePath missing: $description")
    }
}

function Forbid-Pattern([string]$relativePath, [string]$pattern, [string]$description) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return
    }
    $content = Get-Content -LiteralPath $path -Raw
    if ([regex]::IsMatch($content, $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $script:failures.Add("$relativePath contains forbidden $description")
    }
}

Require-Pattern "main/voice_command_adapter.cpp" 'displayStateSetProjection' "projection setter"
Require-Pattern "main/voice_command_adapter.cpp" 'displayStateSetBrightnessPercent' "brightness setter"
Require-Pattern "main/voice_command_adapter.cpp" 'VOICE_COMMAND_DEFERRED:.*no_display_state_contract' "deferred command log"
Require-Pattern "main/display_runtime.cpp" 'voice_sr_runtime_get_command[\s\S]*voiceCommandAdapterDispatch' "owner-loop dispatch"
Require-Pattern "main/display_runtime.cpp" 'PROTOCOL_V2_STATE_READY' "protocol-ready state log"
Require-Pattern "main/voice_sr_runtime.cpp" 'she zhi liang du liu shi' "exact brightness 60 phrase"
Require-Pattern "main/voice_sr_runtime.cpp" 'VOICE_COMMAND_BRIGHTNESS_UP[\s\S]*ti gao liang du[\s\S]*liang yi dian[\s\S]*zeng jia liang du' "brightness-up phrases"
Require-Pattern "main/voice_sr_runtime.cpp" 'VOICE_COMMAND_BRIGHTNESS_DOWN[\s\S]*jiang di liang du[\s\S]*an yi dian[\s\S]*jian shao liang du' "brightness-down phrases"
Require-Pattern "main/main.cpp" 'READY_FOR_VOICE_COMMAND_TEST' "manual test readiness gate"

Forbid-Pattern "main/voice_command_adapter.cpp" '(?i)lvgl|lv_|NimBLE|displayBle|notify|displayProtocolV2|ui_' "UI, BLE, or protocol dependency"
Forbid-Pattern "main/voice_sr_runtime.cpp" 'displayStateSet|displayBle|lv_' "voice-task state, BLE, or LVGL mutation"

if ($failures.Count -gt 0) {
    Write-Host "VOICE_COMMAND_ADAPTER_VERIFY: FAIL ($($failures.Count))"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

& g++ -std=c++17 -Wall -Wextra -Werror `
    -DENABLE_DISPLAY_STATE_LOG=0 `
    -I $hostTestDir `
    -I $productDir `
    -I (Join-Path $projectRoot "main") `
    (Join-Path $hostTestDir "voice_command_adapter_test.cpp") `
    (Join-Path $projectRoot "main/voice_command.cpp") `
    (Join-Path $projectRoot "main/voice_command_adapter.cpp") `
    (Join-Path $productDir "display_state.cpp") `
    (Join-Path $productDir "display_product_page.cpp") `
    (Join-Path $productDir "display_protocol_v2.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testBinary
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "VOICE_COMMAND_ADAPTER_TEST: PASS brightness_relative_cases=4"
Write-Host "VOICE_COMMAND_ADAPTER_VERIFY: PASS"
