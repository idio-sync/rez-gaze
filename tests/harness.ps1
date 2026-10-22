# Runs proxy_harness twice in out\harness: normally, then with rez_gaze\ moved aside
# (--helper-missing). Afterwards no helper from out\harness may still be running.
param([Parameter(Mandatory = $true)][string]$Dir)

# CTest passes forward slashes; process paths use backslashes.
$Dir = (Resolve-Path $Dir).Path

$exe = Join-Path $Dir 'proxy_harness.exe'
$helperDir = Join-Path $Dir 'rez_gaze'
$aside = Join-Path $Dir 'rez_gaze_aside'
$fail = $false

function HelpersLeft { @(Get-Process rez_gaze_helper -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Dir*" }) }

& $exe
if ($LASTEXITCODE -ne 0) { $fail = $true }
# The helper exits within 1 s of its parent (the harness); wait for it before moving its folder.
for ($i = 0; $i -lt 20 -and (HelpersLeft).Count -gt 0; $i++) { Start-Sleep -Milliseconds 250 }

if (Test-Path $aside) { Remove-Item -Recurse -Force $aside }
Rename-Item $helperDir $aside
try {
    & $exe --helper-missing
    if ($LASTEXITCODE -ne 0) { $fail = $true }
} finally {
    Rename-Item $aside $helperDir
}

Start-Sleep -Seconds 1
$left = HelpersLeft
if ($left.Count -gt 0) { Write-Output "FAIL: $($left.Count) helper(s) still running after the harness exited"; $fail = $true }

if ($fail) { exit 1 }
Write-Output 'PASS: harness'
exit 0
