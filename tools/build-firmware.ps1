[CmdletBinding()]
param(
    [string]$ProjectPath = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildRoot = '',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')]
    [string]$BuildName = 'firmware',
    [string]$PythonPath = ''
)
$ErrorActionPreference = 'Stop'
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $ProjectPath 'platformio.ini') -PathType Leaf)) {
    throw "ProjectPath must contain platformio.ini: $ProjectPath"
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) { $BuildRoot = Join-Path $ProjectPath '.build' }
elseif (-not [System.IO.Path]::IsPathRooted($BuildRoot)) { $BuildRoot = Join-Path $ProjectPath $BuildRoot }
$BuildRoot = [System.IO.Path]::GetFullPath($BuildRoot)

if ([string]::IsNullOrWhiteSpace($PythonPath)) {
    $PythonPath = Join-Path $ProjectPath '.venv/Scripts/python.exe'
    if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
        $PythonPath = Join-Path $ProjectPath '.venv/bin/python'
    }
} elseif (-not [System.IO.Path]::IsPathRooted($PythonPath)) {
    $PythonPath = Join-Path $ProjectPath $PythonPath
}
if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
    throw 'Python was not found. Create .venv and install platformio==6.1.19 as described in docs/LUA-BUILD.md, or pass -PythonPath.'
}
$PythonPath = (Resolve-Path -LiteralPath $PythonPath).ProviderPath

$logDirectory = Join-Path $BuildRoot 'logs'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$logPath = Join-Path $logDirectory "$BuildName-build.log"
$settings = @{
    PLATFORMIO_CORE_DIR = (Join-Path $BuildRoot 'platformio-core')
    PLATFORMIO_BUILD_DIR = (Join-Path $BuildRoot "build/$BuildName")
    PLATFORMIO_LIBDEPS_DIR = (Join-Path $BuildRoot "libdeps/$BuildName")
    PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
    PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
}
$previousSettings = @{}
foreach ($name in $settings.Keys) {
    $previousSettings[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $settings[$name], 'Process')
}
try {
    # Native stderr can contain ordinary compiler warnings. Report failures by
    # the process exit code while retaining both streams in the build log.
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    # Native processes update the global automatic variable. A local assignment
    # would shadow that update and falsely report the sentinel after success.
    $global:LASTEXITCODE = 1
    & $PythonPath -m platformio run --project-dir $ProjectPath --environment esp32s3box 2>&1 |
        Tee-Object -FilePath $logPath -ErrorAction Stop
    $result = $global:LASTEXITCODE
} finally {
    $ErrorActionPreference = 'Stop'
    foreach ($name in $previousSettings.Keys) {
        if ($null -eq $previousSettings[$name]) {
            # Preserve absence, not an empty variable (PowerShell 7 distinction).
            [Environment]::SetEnvironmentVariable($name, [NullString]::Value, 'Process')
        } else {
            [Environment]::SetEnvironmentVariable($name, $previousSettings[$name], 'Process')
        }
    }
}
if ($result -ne 0) { throw "Firmware build failed with exit code $result; see $logPath" }
Write-Output "Firmware build passed: $(Join-Path $BuildRoot "build/$BuildName/esp32s3box/firmware.bin")"
Write-Output "Build log: $logPath"
