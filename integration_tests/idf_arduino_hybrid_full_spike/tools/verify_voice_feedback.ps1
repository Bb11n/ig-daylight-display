$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

function Require([string]$path, [string]$pattern, [string]$description) {
    $content = Get-Content -LiteralPath (Join-Path $projectRoot $path) -Raw
    if (-not [regex]::IsMatch($content, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $script:failures.Add("$path missing: $description")
    }
}

function Forbid([string]$path, [string]$pattern, [string]$description) {
    $content = Get-Content -LiteralPath (Join-Path $projectRoot $path) -Raw
    if ([regex]::IsMatch($content, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $script:failures.Add("$path contains forbidden: $description")
    }
}

Require "main/voice_audio_adapter.cpp" 'i2s_new_channel\s*\(\s*&channelConfig\s*,\s*&speakerChannel\s*,\s*&microphoneChannel' "single duplex I2S allocation"
Require "main/voice_audio_adapter.cpp" 'codec_mode\s*=\s*ESP_CODEC_DEV_WORK_MODE_DAC' "ES8311 DAC mode"
Require "main/voice_audio_adapter.cpp" 'pa_pin\s*=\s*GPIO_NUM_46' "PA GPIO 46"
Require "main/voice_feedback_runtime.cpp" 'xQueueCreate\s*\(\s*1' "bounded queue depth 1"
Require "main/voice_feedback_runtime.cpp" 'xQueueOverwrite' "latest-event overwrite"
Require "main/voice_feedback_runtime.cpp" 'VOICE_COMMAND_SUPPRESSED: reason=feedback_playing' "feedback suppression log"
Require "main/voice_feedback_runtime.cpp" 'kCooldownMs\s*=\s*500' "post-playback cooldown"
Require "main/voice_feedback_runtime.cpp" 'fread\s*\(buffer' "streamed WAV reads"
Require "main/voice_feedback_runtime.cpp" 'format_if_mount_failed\s*=\s*false' "non-destructive SD mount"
Require "main/voice_feedback_runtime.cpp" 'projector_on\.wav' "projector on mapping"
Require "main/voice_feedback_runtime.cpp" 'projector_off\.wav' "projector off mapping"
Require "main/voice_feedback_runtime.cpp" 'value\s*==\s*60.*brightness\.wav' "brightness 60 mapping"
Require "main/voice_sr_runtime.cpp" 'voiceFeedbackShouldSuppressCommand' "recognition dispatch suppression"
Require "main/voice_command_adapter.cpp" 'displayStateSetProjection[\s\S]*voiceFeedbackEnqueue' "state update before feedback enqueue"
Forbid "main/voice_feedback_runtime.cpp" '(?i)NimBLE|displayBle|lvgl|lv_' "BLE or LVGL dependency"
Forbid "main/voice_feedback_runtime.cpp" '\bi2s_new_channel\b' "second I2S controller allocation"

if ($failures.Count -gt 0) {
    Write-Host "VOICE_FEEDBACK_VERIFY: FAIL ($($failures.Count))"
    $failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "VOICE_FEEDBACK_VERIFY: PASS checks=16"
