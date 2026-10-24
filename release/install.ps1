# Installs (or updates) rez-gaze into Rez Infinite's REZ folder.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-RezDir '<...>\Rez Infinite\REZ']
#
# Rez's own openvr_api.dll is kept as openvr_api_real.dll (recognised by its SHA-256; anything
# else is left alone and the install stops). An existing rez_gaze.ini is kept.
param(
    [string]$RezDir,
    [string]$PackageDir = $PSScriptRoot,
    [string]$OriginalHash
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $OriginalHash) { $OriginalHash = $RezOpenVrHash }

Assert-RezClosed
$RezDir = Resolve-RezDir $RezDir
$dll = Join-Path $RezDir 'openvr_api.dll'
$real = Join-Path $RezDir 'openvr_api_real.dll'

if (Test-Path -LiteralPath $real) {
    if ((Sha256 $real) -ne $OriginalHash) {
        Fail "openvr_api_real.dll in $RezDir isn't Rez's original; not touching anything"
    }
    Write-Host 'Updating an existing rez-gaze install.'
} else {
    if (-not (Test-Path -LiteralPath $dll)) { Fail "no openvr_api.dll in $RezDir" }
    if ((Sha256 $dll) -ne $OriginalHash) {
        Fail "openvr_api.dll in $RezDir isn't Rez's original (another mod, or a different game version); not touching it"
    }
    Copy-Item -LiteralPath $dll -Destination $real
    Write-Host "Kept Rez's openvr_api.dll as openvr_api_real.dll."
}

# The stand-in forwards to "openvr_api_real", and Windows asks for openvr_api_real.DLL. On a
# case-sensitive folder (some network shares) that's a different file, so add that name too.
$upper = Join-Path $RezDir 'openvr_api_real.DLL'
if (-not (Test-Path -LiteralPath $upper)) {
    Copy-Item -LiteralPath $real -Destination $upper
    Write-Host 'Case-sensitive folder: also added openvr_api_real.DLL.'
}

Copy-Item -LiteralPath (Join-Path $PackageDir 'openvr_api.dll') -Destination $dll -Force
$helperDir = Join-Path $RezDir 'rez_gaze'
New-Item -ItemType Directory -Force -Path $helperDir | Out-Null
Copy-Item -Path (Join-Path $PackageDir 'rez_gaze\*') -Destination $helperDir -Recurse -Force
$ini = Join-Path $RezDir 'rez_gaze.ini'
if (Test-Path -LiteralPath $ini) {
    Write-Host 'Kept your rez_gaze.ini.'
} else {
    Copy-Item -LiteralPath (Join-Path $PackageDir 'rez_gaze.ini') -Destination $ini
}

Write-Host ''
Write-Host "rez-gaze installed in $RezDir" -ForegroundColor Green
Write-Host 'Before playing:'
Write-Host '  - SteamVR > Settings > Steam Link: turn on "Share eye tracking data to other apps on this PC"'
Write-Host '    (and "Enable OSC" for the fallback gaze source).'
Write-Host '  - In Rez: Options > VR > control type "motion controller only".'
Write-Host '  - Start SteamVR with the Frame before Rez. Settings: rez_gaze.ini; log: rez_gaze.log.'
exit 0
