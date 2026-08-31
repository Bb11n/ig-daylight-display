$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

function Require-File([string]$relativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relativePath) -PathType Leaf)) {
        $script:failures.Add("missing file: $relativePath")
    }
}

function Require-Pattern([string]$relativePath, [string]$pattern, [string]$description) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
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

foreach ($file in @(
        "main/voice_audio_adapter.h",
        "main/voice_audio_adapter.cpp",
        "main/voice_command.h",
        "main/voice_command.cpp",
        "main/voice_command_adapter.h",
        "main/voice_command_adapter.cpp",
        "main/voice_sr_runtime.h",
        "main/voice_sr_runtime.cpp")) {
    Require-File $file
}

Require-Pattern "main/idf_component.yml" 'espressif/esp-sr:\s*\r?\n\s+version:\s*"?=2\.4\.6"?' "exact ESP-SR 2.4.6 dependency"
Require-Pattern "main/idf_component.yml" 'espressif/esp_codec_dev:\s*\r?\n\s+version:\s*"?=1\.5\.11"?' "exact esp_codec_dev 1.5.11 dependency"
Require-Pattern "partitions.csv" '^model,\s*data,\s*spiffs,.*0x400000' "4 MiB speech model partition"
Require-Pattern "sdkconfig.defaults" '^CONFIG_SR_MN_CN_MULTINET6_QUANT=y$' "MultiNet6 Chinese model"
Require-Pattern "sdkconfig.defaults" '^CONFIG_CODEC_ES7210_SUPPORT=y$' "ES7210 codec support"

Require-Pattern "main/voice_audio_adapter.cpp" 'audio_codec_ctrl_if_t' "custom codec control interface"
Require-Pattern "main/voice_audio_adapter.cpp" 'display_runtime_shared_i2c_write' "shared Wire NG write adapter"
Require-Pattern "main/voice_audio_adapter.cpp" 'display_runtime_shared_i2c_read' "shared Wire NG read adapter"
Require-Pattern "main/voice_audio_adapter.cpp" 'GPIO_NUM_9' "I2S BCLK pin"
Require-Pattern "main/voice_audio_adapter.cpp" 'GPIO_NUM_42' "I2S MCLK pin"
Require-Pattern "main/voice_audio_adapter.cpp" 'GPIO_NUM_45' "I2S WS pin"
Require-Pattern "main/voice_audio_adapter.cpp" 'GPIO_NUM_10' "I2S microphone data pin"
Forbid-Pattern "main/voice_audio_adapter.cpp" 'Wire\.begin|i2c_new_master_bus|audio_codec_new_i2c_ctrl|BSP_' "second I2C owner or Waveshare BSP"

Require-Pattern "main/voice_sr_runtime.cpp" 'esp_srmodel_init\s*\(\s*"model"\s*\)' "model partition load"
Require-Pattern "main/voice_sr_runtime.cpp" 'mn6_cn' "MultiNet6 Chinese runtime"
Require-Pattern "main/voice_sr_runtime.cpp" 'VOICE_RECOGNIZED command_id=' "Gate 1 recognition log"
Require-Pattern "main/voice_sr_runtime.cpp" 'xTaskCreatePinnedToCore\s*\([\s\S]*?kVoiceTaskStackBytes[\s\S]*?kVoiceTaskPriority[\s\S]*?kVoiceTaskCore' "8192-byte priority-5 Core-1 voice task"
Require-Pattern "main/voice_sr_runtime.cpp" 'kVoiceTaskStackBytes\s*=\s*8192' "voice task stack size"
Require-Pattern "main/voice_sr_runtime.cpp" 'kVoiceTaskPriority\s*=\s*5' "voice task priority"
Require-Pattern "main/voice_sr_runtime.cpp" 'kVoiceTaskCore\s*=\s*1' "voice task Core 1 affinity"
Require-Pattern "main/voice_sr_runtime.cpp" 'audio_max_us=.*afe_max_us=.*mn_max_us=.*loop_max_us=' "periodic maximum timing metrics"
Require-Pattern "main/voice_sr_runtime.cpp" 'voiceFeedbackShouldSuppressCommand' "feedback playback command suppression"
Forbid-Pattern "main/voice_sr_runtime.cpp" 'displayStateSet|displayBle|lv_' "Gate 1 state, BLE, or LVGL mutation"

Require-Pattern "main/main.cpp" 'voice_sr_runtime_init\s*\(' "voice runtime initialization"
Require-Pattern "main/main.cpp" 'display_runtime_start_ble\s*\(\)[\s\S]*voice_sr_runtime_init\s*\(' "BLE-before-voice initialization order"
Require-Pattern "main/main.cpp" 'READY_FOR_VOICE_TEST' "voice test readiness gate"
Require-Pattern "CMakeLists.txt" 'IG_ENABLE_GPS_RUNTIME\s+"0"' "GPS runtime disabled by default"

$resourceStages = [ordered]@{
    "MEM_BEFORE_BLE" = "main/display_runtime.cpp"
    "MEM_AFTER_BLE" = "main/display_runtime.cpp"
    "MEM_BEFORE_CODEC" = "main/voice_sr_runtime.cpp"
    "MEM_AFTER_CODEC" = "main/voice_sr_runtime.cpp"
    "MEM_BEFORE_I2S" = "main/voice_audio_adapter.cpp"
    "MEM_AFTER_I2S" = "main/voice_audio_adapter.cpp"
    "MEM_BEFORE_AFE" = "main/voice_sr_runtime.cpp"
    "MEM_AFTER_AFE" = "main/voice_sr_runtime.cpp"
    "MEM_AFTER_MULTINET" = "main/voice_sr_runtime.cpp"
    "MEM_VOICE_RUNNING" = "main/voice_sr_runtime.cpp"
}
foreach ($stage in $resourceStages.Keys) {
    Require-Pattern $resourceStages[$stage] $stage "resource checkpoint $stage"
}

Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y$' "official NimBLE external-memory allocation"
Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_SECURITY_ENABLE=n$' "unused NimBLE security disabled"
Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1$' "single BLE connection"
Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_MAX_CCCDS=1$' "single CCCD"
Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=64$' "64-byte preferred ATT MTU"
Require-Pattern "sdkconfig.defaults" '^CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n$' "unused BLE 5 features disabled"

$newSources = @("main/voice_audio_adapter.cpp", "main/voice_sr_runtime.cpp")
foreach ($source in $newSources) {
    Forbid-Pattern $source '(?i)bsp/|ble_server|ble_protocol|esp_wn_|wn9_|i2s_channel_write\s*\(|esp_codec_dev_write\s*\(' "excluded product or direct audio-output code"
}

if ($failures.Count -gt 0) {
    Write-Host "FINAL_VOICE_SPIKE_VERIFY: FAIL ($($failures.Count))"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "FINAL_VOICE_SPIKE_VERIFY: PASS"
