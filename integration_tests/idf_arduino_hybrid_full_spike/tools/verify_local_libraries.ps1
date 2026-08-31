$ErrorActionPreference = "Stop"

if ($env:IG_ARDUINO_LIB_ROOT) {
    $libraryRoot = $env:IG_ARDUINO_LIB_ROOT
} else {
    $libraryRoot = Join-Path $env:USERPROFILE "Documents/Arduino/libraries"
}

$requirements = @(
    @{
        Directory = "GFX_Library_for_Arduino"
        Version = "1.6.6"
        Header = "src/Arduino_GFX_Library.h"
    },
    @{
        Directory = "SensorLib"
        Version = "0.3.1"
        Header = "src/TouchDrvCSTXXX.hpp"
    },
    @{
        Directory = "XPowersLib"
        Version = "0.2.6"
        Header = "src/XPowersLib.h"
    }
)

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($requirement in $requirements) {
    $directory = Join-Path $libraryRoot $requirement.Directory
    $properties = Join-Path $directory "library.properties"
    $header = Join-Path $directory $requirement.Header
    if (-not (Test-Path -LiteralPath $properties -PathType Leaf)) {
        $failures.Add("missing library metadata: $properties")
        continue
    }
    $metadata = Get-Content -LiteralPath $properties -Raw
    $expectedVersion = [regex]::Escape($requirement.Version)
    if ($metadata -notmatch "(?m)^version=$expectedVersion\s*$") {
        $failures.Add("unexpected $($requirement.Directory) version; expected $($requirement.Version)")
    }
    if (-not (Test-Path -LiteralPath $header -PathType Leaf)) {
        $failures.Add("missing required header: $header")
        continue
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $header).Hash
    Write-Host "PHASE2_LOCAL_LIBRARY: name=$($requirement.Directory) version=$($requirement.Version) header_sha256=$hash"
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$pinConfig = Join-Path $projectRoot "main/pin_config.h"
if (-not (Test-Path -LiteralPath $pinConfig -PathType Leaf)) {
    $failures.Add("missing project pin config: $pinConfig")
}

if ($failures.Count -gt 0) {
    Write-Host "PHASE2_LOCAL_LIBRARY_VERIFY: FAIL"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "PHASE2_LOCAL_LIBRARY_VERIFY: PASS root=$libraryRoot"
