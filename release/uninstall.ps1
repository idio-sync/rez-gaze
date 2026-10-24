# Removes rez-gaze from Rez Infinite's REZ folder and puts Rez's own openvr_api.dll back.
# rez_gaze.ini and rez_gaze.log are left in place.
#
#   powershell -ExecutionPolicy Bypass -File uninstall.ps1 [-RezDir '<...>\Rez Infinite\REZ']
param(
    [string]$RezDir,
    [string]$OriginalHash
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $OriginalHash) { $OriginalHash = $RezOpenVrHash }

Assert-RezClosed
$RezDir = Resolve-RezDir $RezDir
$dll = Join-Path $RezDir 'openvr_api.dll'
$real = Join-Path $RezDir 'openvr_api_real.dll'

if (-not (Test-Path -LiteralPath $real)) { Fail "rez-gaze doesn't look installed in $RezDir (no openvr_api_real.dll)" }
if ((Sha256 $real) -ne $OriginalHash) { Fail "openvr_api_real.dll in $RezDir isn't Rez's original; not touching anything" }

Copy-Item -LiteralPath $real -Destination $dll -Force
Remove-Item -LiteralPath $real
# On a case-sensitive folder, the install also added openvr_api_real.DLL.
$upper = Join-Path $RezDir 'openvr_api_real.DLL'
if ((Test-Path -LiteralPath $upper) -and ((Sha256 $upper) -eq $OriginalHash)) { Remove-Item -LiteralPath $upper }
$helperDir = Join-Path $RezDir 'rez_gaze'
if (Test-Path -LiteralPath $helperDir) { Remove-Item -LiteralPath $helperDir -Recurse -Force }

Write-Host "rez-gaze removed; Rez's own openvr_api.dll is back. rez_gaze.ini and rez_gaze.log were left." -ForegroundColor Green
exit 0
