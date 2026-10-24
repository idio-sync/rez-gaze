# Builds the release zip from a build folder:
#
#   powershell -ExecutionPolicy Bypass -File release\package.ps1 -Out out -Version dev [-Dest dist]
#
# The zip holds one folder, rez-gaze-<version>\, laid out like Rez's REZ folder plus the
# install scripts. CI runs this for the rolling nightly (.github/workflows/nightly.yml).
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Dest = 'dist'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$name = "rez-gaze-$Version"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("rez_gaze_stage_" + [guid]::NewGuid().ToString('N'))
$root = Join-Path $stage $name

function Need([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { Write-Host "package: missing $path (build first)" -ForegroundColor Red; exit 1 }
    return $path
}

try {
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'rez_gaze') | Out-Null
    Copy-Item (Need (Join-Path $Out 'proxy\openvr_api.dll')) (Join-Path $root 'openvr_api.dll')
    Copy-Item (Need (Join-Path $Out 'rez_gaze\rez_gaze_helper.exe')) (Join-Path $root 'rez_gaze\')
    Copy-Item (Need (Join-Path $Out 'rez_gaze\openvr_api.dll')) (Join-Path $root 'rez_gaze\')
    foreach ($f in 'install.ps1', 'uninstall.ps1', 'common.ps1', 'rez_gaze.ini', 'README.txt') {
        Copy-Item (Need (Join-Path $PSScriptRoot $f)) (Join-Path $root $f)
    }
    Copy-Item (Need (Join-Path $repo 'third_party\openvr\LICENSE')) (Join-Path $root 'LICENSE-OpenVR.txt')
    Set-Content -LiteralPath (Join-Path $root 'VERSION.txt') -Value $Version -Encoding ASCII

    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    $zip = Join-Path (Resolve-Path $Dest).Path "$name.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip)
    Write-Host "package: $zip"
    exit 0
} finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}
