[CmdletBinding()]
param(
    [string]$PythonPath = 'python',
    [string]$CoreDir = '',
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project '.build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$settings = @{
    PLATFORMIO_BUILD_DIR = (Join-Path $build 'firmware')
    PLATFORMIO_LIBDEPS_DIR = (Join-Path $build 'libdeps')
    PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
}
if ($CoreDir) { $settings.PLATFORMIO_CORE_DIR = [IO.Path]::GetFullPath($CoreDir) }
$previous = @{}
foreach ($name in $settings.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    [Environment]::SetEnvironmentVariable($name,$settings[$name],'Process')
}
try {
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    & $PythonPath -m platformio run --project-dir $project -e esp32s3box -j $Jobs 2>&1 |
        Tee-Object -FilePath (Join-Path $build 'firmware-build.log') -ErrorAction Stop
    $result = $LASTEXITCODE
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')
    }
}
if ($result -ne 0) { throw "NES firmware build failed ($result). See .build/firmware-build.log." }
Write-Output (Join-Path $build 'firmware/esp32s3box/firmware.bin')
