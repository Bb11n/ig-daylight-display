$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Resolve-Path (Join-Path $projectRoot "../..")
$productRoot = Join-Path $repositoryRoot "firmware/esp32_s3_touch_amoled_1_75/ui_static_mock"
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Require-File([string]$relativePath) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Failure "missing file: $relativePath"
    }
}

function Require-Pattern(
    [string]$basePath,
    [string]$relativePath,
    [string]$pattern,
    [string]$description
) {
    $path = Join-Path $basePath $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return
    }
    $content = (Get-Content -LiteralPath $path -Raw) -replace "`r`n", "`n"
    if (-not [regex]::IsMatch(
            $content,
            $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        Add-Failure "$relativePath missing: $description"
    }
}

function Forbid-Pattern(
    [string]$basePath,
    [string]$relativePath,
    [string]$pattern,
    [string]$description
) {
    $path = Join-Path $basePath $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return
    }
    $content = Get-Content -LiteralPath $path -Raw
    if ([regex]::IsMatch(
            $content,
            $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        Add-Failure "$relativePath contains forbidden $description"
    }
}

$requiredFiles = @(
    "CMakeLists.txt",
    "sdkconfig.defaults",
    "partitions.csv",
    ".gitignore",
    "README.md",
    "PHASE2_BLE_RETENTION_AUDIT.md",
    "main/CMakeLists.txt",
    "main/idf_component.yml",
    "main/main.cpp",
    "main/display_runtime.h",
    "main/display_runtime.cpp",
    "main/i2c_telemetry.h",
    "main/i2c_telemetry.cpp",
    "main/spike_resource_monitor.cpp",
    "main/pin_config.h",
    "components/arduino_gfx/CMakeLists.txt",
    "components/sensor_lib/CMakeLists.txt",
    "components/xpowers_lib/CMakeLists.txt",
    "tools/verify_local_libraries.ps1"
)
foreach ($file in $requiredFiles) {
    Require-File $file
}

Require-Pattern $projectRoot "main/idf_component.yml" 'idf:\s*"?=5\.5\.4"?' "exact IDF 5.5.4 dependency"
Require-Pattern $projectRoot "main/idf_component.yml" 'espressif/arduino-esp32:\s*\r?\n\s+version:\s*"?=3\.3\.10"?' "exact Arduino 3.3.10 dependency"
Require-Pattern $projectRoot "main/idf_component.yml" 'lvgl/lvgl:\s*\r?\n\s+version:\s*"?=8\.4\.0"?' "exact LVGL 8.4.0 dependency"
Require-Pattern $projectRoot "main/idf_component.yml" 'h2zero/esp-nimble-cpp:\s*\r?\n\s+version:\s*"?=2\.5\.0"?' "exact esp-nimble-cpp 2.5.0 dependency"
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "main/voice_sr_runtime.cpp") -PathType Leaf)) {
    Forbid-Pattern $projectRoot "main/idf_component.yml" '(?i)esp-sr' "ESP-SR dependency"
}

$configPatterns = [ordered]@{
    '^CONFIG_IDF_TARGET="esp32s3"$' = "ESP32-S3 target"
    '^CONFIG_FREERTOS_HZ=1000$' = "1000 Hz FreeRTOS tick"
    '^CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y$' = "16 MB flash"
    '^CONFIG_SPIRAM_MODE_OCT=y$' = "Octal PSRAM"
    '^CONFIG_SPIRAM_SPEED_40M=y$' = "40 MHz PSRAM"
    '^CONFIG_AUTOSTART_ARDUINO=n$' = "disabled Arduino autostart"
    '^CONFIG_ARDUINO_SELECTIVE_COMPILATION=y$' = "Arduino selective compilation"
    '^CONFIG_ARDUINO_SELECTIVE_SPI=y$' = "Arduino SPI source"
    '^CONFIG_ARDUINO_SELECTIVE_Wire=y$' = "Arduino Wire source"
    '^CONFIG_ARDUINO_SELECTIVE_BLE=n$' = "disabled Bluedroid Arduino BLE library"
    '^CONFIG_ARDUINO_SELECTIVE_ESP_SR=n$' = "disabled Arduino ESP-SR wrapper"
    '^CONFIG_BT_ENABLED=y$' = "Bluetooth controller"
    '^CONFIG_BT_NIMBLE_ENABLED=y$' = "NimBLE host"
    '^CONFIG_PARTITION_TABLE_CUSTOM=y$' = "custom product-sized partition table"
    '^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"$' = "partition table filename"
}
foreach ($entry in $configPatterns.GetEnumerator()) {
    Require-Pattern $projectRoot "sdkconfig.defaults" $entry.Key $entry.Value
}

$mainPath = Join-Path $projectRoot "main/main.cpp"
if (Test-Path -LiteralPath $mainPath -PathType Leaf) {
    $mainContent = Get-Content -LiteralPath $mainPath -Raw
    $appMainCount = ([regex]::Matches($mainContent, '\bapp_main\s*\(')).Count
    if ($appMainCount -ne 1) {
        Add-Failure "main/main.cpp app_main count is $appMainCount, expected 1"
    }
}

foreach ($entry in ([ordered]@{
    '#include\s+"esp32-hal-alloc-ble-mem\.h"' = "Arduino BLE memory-retention marker"
    '#include\s+"esp32-hal-bt\.h"' = "Arduino BLE retention status API"
    '\binitArduino\s*\(' = "initArduino call"
    '\bdisplay_runtime_init\s*\(' = "display runtime init call"
    '\bdisplay_runtime_loop\s*\(' = "display runtime loop call"
    'console_transport=USB_SERIAL_JTAG' = "USB Serial/JTAG observability log"
    'PHASE2_SPIKE: app_main begin' = "Phase 2 startup log"
    'ARDUINO_BLE_RETENTION: registered=%d before_init_arduino=1 controller_status=%d' = "BLE retention pre-init log"
}).GetEnumerator()) {
    Require-Pattern $projectRoot "main/main.cpp" $entry.Key $entry.Value
}

if (Test-Path -LiteralPath $mainPath -PathType Leaf) {
    $retentionLogIndex = $mainContent.IndexOf("ARDUINO_BLE_RETENTION:")
    $initArduinoIndex = $mainContent.IndexOf("initArduino();")
    if ($retentionLogIndex -lt 0 -or $initArduinoIndex -lt 0 -or
        $retentionLogIndex -gt $initArduinoIndex) {
        Add-Failure "BLE retention registration log must appear before initArduino()"
    }
}

Forbid-Pattern $projectRoot "main/main.cpp" '\bsetup\s*\(' "Arduino setup entry copied into app_main"
Forbid-Pattern $projectRoot "main/main.cpp" '\bloop\s*\(' "Arduino loop entry copied into app_main"
Forbid-Pattern $projectRoot "main/main.cpp" '(?i)wakenet|multinet|audio_codec|i2s_|esp_srmodel|esp_mn_' "speech or audio runtime"

Require-Pattern $projectRoot "main/display_runtime.h" 'bool\s+display_runtime_init\s*\(\s*\)' "display_runtime_init API"
Require-Pattern $projectRoot "main/display_runtime.h" 'void\s+display_runtime_loop\s*\(\s*\)' "display_runtime_loop API"
Require-Pattern $projectRoot "main/display_runtime.cpp" '#define\s+setup\s+display_runtime_sketch_setup' "setup adapter rename"
Require-Pattern $projectRoot "main/display_runtime.cpp" '#define\s+loop\s+display_runtime_sketch_loop' "loop adapter rename"
Require-Pattern $projectRoot "main/display_runtime.cpp" 'ui_static_mock\.ino' "existing sketch source reference"
Require-Pattern $projectRoot "main/display_runtime.cpp" 'displayBleServerReady' "BLE readiness bridge"
Require-Pattern $projectRoot "main/display_runtime.cpp" 'PHASE_A_I2C_ONLY' "delayed BLE Phase A"
Require-Pattern $projectRoot "main/display_runtime.cpp" 'READY_FOR_PHONE_CONNECTION' "phone connection gate"
Require-Pattern $projectRoot "main/i2c_telemetry.cpp" '__wrap_i2cWrite' "Wire NG write telemetry wrapper"
Require-Pattern $projectRoot "main/i2c_telemetry.cpp" '__wrap_i2cRead' "Wire NG read telemetry wrapper"
Require-Pattern $projectRoot "main/i2c_telemetry.cpp" 'I2C_FIRST_INVALID_STATE:' "first invalid-state context log"
Require-Pattern $projectRoot "main/i2c_telemetry.cpp" 'kGpsReadAddress\s*=\s*0x54' "LC76G read-address attribution"
Require-Pattern $projectRoot "main/CMakeLists.txt" '--wrap=i2cWrite' "I2C HAL linker wrapping"
Require-Pattern $projectRoot "CMakeLists.txt" 'IG_I2C_VARIANT' "I2C module-bisection selector"
Require-Pattern $projectRoot "CMakeLists.txt" 'IG_ENABLE_GPS_RUNTIME\s+"1"' "full-product GPS runtime enabled default"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ENABLE_GPS_RUNTIME=\$\{IG_ENABLE_GPS_RUNTIME\}' "GPS runtime compile definition"
Require-Pattern $productRoot "ui_static_mock.ino" '#if\s+!ENABLE_GPS_RUNTIME[\s\S]*?#undef\s+DIAG_ENABLE_GPS[\s\S]*?#define\s+DIAG_ENABLE_GPS\s+0' "GPS runtime hard gate"

Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_home_status\.cpp' "HomeStatus source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_home_mode\.cpp' "HomeMode source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_workout_gps\.cpp' "WorkoutGPS source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_ig_control\.cpp' "IG Control source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_settings\.cpp' "Settings source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'ui_battery_ring\.cpp' "Global Battery Ring source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'display_ble_server\.cpp' "Display BLE source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'display_protocol_v2\.cpp' "Protocol v2 source"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'gps_parser\.cpp' "GPS parser source"
Forbid-Pattern $projectRoot "main/CMakeLists.txt" '\$\{PRODUCT_UI_DIR\}/system_resource_monitor\.cpp' "original UART-only resource monitor source"

Require-Pattern $productRoot "ui_static_mock.ino" 'lv_disp_draw_buf_init\s*\(\s*&drawBuf\s*,\s*lvBuf1\s*,\s*nullptr' "LVGL single-buffer initialization"
Require-Pattern $productRoot "ui_static_mock.ino" 'ui_theme::kScreenWidth\s*\*\s*ui_theme::kScreenHeight\s*/\s*4' "108578-pixel draw-buffer calculation"
Require-Pattern $productRoot "ui_static_mock.ino" 'Wire\.begin\s*\(\s*kLc76gI2cSda\s*,\s*kLc76gI2cScl\s*,\s*kLc76gI2cFreqHz\s*\)' "single Arduino Wire NG owner initialization"
Require-Pattern $productRoot "ui_static_mock.ino" 'owner=ARDUINO_WIRE_NG' "Arduino Wire NG owner startup log"
Require-Pattern $productRoot "ui_static_mock.ino" 'init_count=' "single shared I2C initialization count log"
Require-Pattern $productRoot "ui_static_mock.ino" 'I2C_BUFFER_LENGTH' "Wire buffer-derived transaction chunk size"
Require-Pattern $productRoot "ui_static_mock.ino" 'volatile\s+bool\s+touchInterruptPending\s*=\s*false' "CST9217 interrupt-gated touch state"
Require-Pattern $productRoot "ui_static_mock.ino" 'attachInterrupt\s*\(\s*digitalPinToInterrupt\s*\(\s*TP_INT\s*\)[\s\S]*?FALLING' "CST9217 falling-edge touch trigger"
Require-Pattern $productRoot "ui_static_mock.ino" 'if\s*\(\s*!touchInterruptPending\s*\)' "touch I2C read interrupt gate"
Require-Pattern $projectRoot "main/CMakeLists.txt" 'gps_i2c_transport\.cpp' "GPS chunked I2C transport source"
foreach ($file in @(
        "gps_i2c_transport.h",
        "gps_i2c_transport.cpp",
        "tests/gps_i2c_transport_test.cpp")) {
    if (-not (Test-Path -LiteralPath (Join-Path $productRoot $file) -PathType Leaf)) {
        Add-Failure "missing product file: $file"
    }
}

$legacyI2cPattern = '(?i)driver/i2c\.h|\bi2c_driver_install\b|\bi2c_driver_delete\b|\bi2c_param_config\b|\bi2c_master_cmd_begin\b|\bi2c_cmd_link_create\b'
Forbid-Pattern $productRoot "ui_static_mock.ino" $legacyI2cPattern "legacy ESP-IDF I2C API"

$productSources = Get-ChildItem -LiteralPath $productRoot -File | Where-Object {
    $_.Extension -in @(".ino", ".cpp", ".h")
}
$wireBeginCount = 0
foreach ($source in $productSources) {
    $sourceContent = Get-Content -LiteralPath $source.FullName -Raw
    $wireBeginCount += ([regex]::Matches($sourceContent, '\bWire\.begin\s*\(')).Count
}
if ($wireBeginCount -ne 1) {
    Add-Failure "product Wire.begin count is $wireBeginCount, expected exactly 1"
}

$compileCommandsPath = Join-Path $projectRoot "build/compile_commands.json"
if (Test-Path -LiteralPath $compileCommandsPath -PathType Leaf) {
    $compileCommands = Get-Content -LiteralPath $compileCommandsPath -Raw
    if ($compileCommands -match '(?i)USEING_I2C_LEGACY') {
        Add-Failure "compile_commands.json enables the SensorLib legacy I2C backend"
    }
    if ($compileCommands -notmatch 'gps_i2c_transport\.cpp') {
        Add-Failure "compile_commands.json does not compile gps_i2c_transport.cpp"
    }
}

$mapPath = Join-Path $projectRoot "build/idf_arduino_hybrid_full_spike.map"
if (Test-Path -LiteralPath $mapPath -PathType Leaf) {
    $mapContent = Get-Content -LiteralPath $mapPath -Raw
    if ($mapContent -match 'libdriver\.a\(i2c\.c\.obj\)' -or
        $mapContent -match '(?m)^\s+0x[0-9a-f]+\s+i2c_driver_install\s*$' -or
        $mapContent -match '(?m)^\s+0x[0-9a-f]+\s+i2c_master_cmd_begin\s*$') {
        Add-Failure "final link map contains a legacy I2C application path"
    }
    if ($mapContent -notmatch 'esp32-hal-i2c-ng\.c\.obj' -or
        $mapContent -notmatch '(?m)^\s+0x[0-9a-f]+\s+i2c_new_master_bus\s*$') {
        Add-Failure "final link map does not contain the Arduino Wire NG owner"
    }
}
Require-Pattern $productRoot "display_ble_server.cpp" 'kDisplayBleAdvertisedName\[\]\s*=\s*"IG_ROUND"' "IG_ROUND advertising name"
Require-Pattern $productRoot "display_ble_server.cpp" 'ADB402C0-B1C6-11ED-AFA1-0242AC120010' "Display Service UUID"
Require-Pattern $productRoot "display_ble_server.cpp" 'ADB40201-B1C6-11ED-AFA1-0242AC120011' "Display State Characteristic UUID"
Require-Pattern $productRoot "display_ble_server.cpp" 'kDisplayBleNotifyIntervalMs\s*=\s*500' "500 ms BLE notify interval"
Require-Pattern $productRoot "display_protocol_v2.h" 'PROTOCOL_V2_HEADER\s*=\s*0xAB' "Protocol v2 packet header"
Require-Pattern $productRoot "display_protocol_v2.h" 'PROTOCOL_V2_PACKET_LENGTH\s*=\s*34' "Protocol v2 packet size"
Require-Pattern $productRoot "display_ble_server.cpp" 'NIMBLE_PROPERTY::READ\s*\|\s*NIMBLE_PROPERTY::NOTIFY' "read and notify properties"

$displayBleSource = Join-Path $productRoot "display_ble_server.cpp"
if (Test-Path -LiteralPath $displayBleSource -PathType Leaf) {
    $displayBleContent = Get-Content -LiteralPath $displayBleSource -Raw
    $nimbleDeviceInitCount = ([regex]::Matches(
            $displayBleContent,
            '\bNimBLEDevice::init\s*\(')).Count
    if ($nimbleDeviceInitCount -ne 1) {
        Add-Failure "product NimBLEDevice::init count is $nimbleDeviceInitCount, expected exactly 1"
    }
}

foreach ($file in $requiredFiles) {
    Forbid-Pattern $projectRoot $file '(?i)BLEDevice\.h|BLEServer\.h|BLE2902\.h' "Bluedroid BLE include"
}

foreach ($file in @(
        "CMakeLists.txt",
        "main/CMakeLists.txt",
        "main/idf_component.yml",
        "main/main.cpp",
        "main/display_runtime.cpp")) {
    Forbid-Pattern $projectRoot $file '(?i)LisenMoucle|WakeNet|MultiNet|Audio Codec' "voice-product source reference"
}

if (Test-Path -LiteralPath $mapPath -PathType Leaf) {
    $mapContent = Get-Content -LiteralPath $mapPath -Raw
    if ($mapContent -notmatch '_setBleLibraryInUse' -or
        $mapContent -notmatch '_bleLibraryInUse') {
        Add-Failure "final link map does not retain Arduino BLE memory registration"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "HYBRID_PHASE2_SPIKE_VERIFY: FAIL"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "HYBRID_PHASE2_SPIKE_VERIFY: PASS"
