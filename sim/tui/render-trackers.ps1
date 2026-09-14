[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Renderer,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$Renderer = (Resolve-Path -LiteralPath $Renderer).ProviderPath
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath
Add-Type -AssemblyName System.Drawing
$scenes = @(& $Renderer --list | Where-Object { $_ -match '^tracker_' })
if ($LASTEXITCODE -ne 0 -or $scenes.Count -eq 0) { throw 'Renderer returned no tracker scenes.' }
$results = foreach ($scene in $scenes) {
    $bmp = Join-Path $OutputDirectory "$scene.bmp"
    $png = Join-Path $OutputDirectory "$scene.png"
    $renderOutput = & $Renderer --scene $scene --out $bmp
    if ($LASTEXITCODE -ne 0) { throw "Render failed for $scene" }
    Write-Host $renderOutput
    $frame = [System.Drawing.Image]::FromFile($bmp)
    try {
        if ($frame.Width -ne 320 -or $frame.Height -ne 240) { throw "Unexpected frame size: $scene" }
        $frame.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $frame.Dispose() }
    [pscustomobject]@{
        scene = $scene
        data = 'deterministic synthetic tracker measurements'
        renderer = 'production TrackerUI templates and LovyanGFX sprite/font primitives'
        width = 320
        height = 240
        png = $png
        sha256 = (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash
    }
}
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).ProviderPath
$sources = foreach ($relative in @('src/app/app_17/tracker_ui.h', 'src/app/app_17/tracker_monitor.h', 'src/app/app_common/mk_tui.h', 'sim/tui/main.cpp')) {
    $path = Join-Path $sourceRoot $relative
    [pscustomobject]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
[pscustomobject]@{
    rendererBinary = $Renderer
    rendererSha256 = (Get-FileHash -LiteralPath $Renderer -Algorithm SHA256).Hash
    sources = @($sources)
    frames = @($results)
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'render-manifest.json') -Encoding utf8
Write-Output "Rendered $($results.Count) production-UI fixtures to $OutputDirectory"
