# Smoke test for rez_gaze_helper.exe: started with a short-lived parent, it must exit within 4 s
# of the parent, and its log must show either "vr init failed" (SteamVR not running) or
# "running" followed by "parent exited". Runs the helper as a background app, which never
# starts SteamVR. With -Fake the helper skips OpenVR and publishes a synthetic gaze, so the
# running path is tested without SteamVR: then "running" + "parent exited" is required, and
# the log's sample count must be over 100.
param([Parameter(Mandatory = $true)][string]$Helper, [switch]$Fake)

if (-not (Test-Path $Helper)) { Write-Output "FAIL: no helper at $Helper"; exit 1 }
$log = Join-Path (Split-Path $Helper) 'rez_gaze_helper.log'
Remove-Item $log -ErrorAction SilentlyContinue

# Parent: lives about 3 s.
$parent = Start-Process -PassThru -WindowStyle Hidden -FilePath ping.exe -ArgumentList '-n', '4', '127.0.0.1'
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Helper
$psi.Arguments = "--parent $($parent.Id) --app background" + $(if ($Fake) { ' --fake' } else { '' })
$psi.UseShellExecute = $false  # a shell-launched process reports no ExitCode
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$parent.WaitForExit()
if (-not $proc.WaitForExit(4000)) {
    $proc.Kill()
    Write-Output "FAIL: helper still running 4 s after its parent exited"
    exit 1
}
$text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
Write-Output "helper exit code $($proc.ExitCode)"
Write-Output $text
if ($Fake) {
    $ok = $text -match 'status running' -and $text -match 'status parent exited' -and
          $text -match '(\d+) samples, \d+ tracked' -and [int64]$Matches[1] -gt 100
    if ($ok -and $proc.ExitCode -eq 0) { Write-Output 'PASS (fake)'; exit 0 }
    Write-Output 'FAIL: fake run lacks running/parent exited, >100 samples, or exit code 0'
    exit 1
}
if ($text -match 'vr init failed') { Write-Output 'PASS (SteamVR not running)'; exit 0 }
if ($text -match 'status running' -and $text -match 'status parent exited') { Write-Output 'PASS'; exit 0 }
Write-Output 'FAIL: log lacks the expected status lines'
exit 1
