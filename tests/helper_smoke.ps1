# Smoke test for rez_gaze_helper.exe: started with a short-lived parent, it must exit within 1 s
# of the parent, and its log must show either "vr init failed" (SteamVR not running) or
# "running" followed by "parent exited". Runs the helper as a background app, which never
# starts SteamVR. With -Fake the helper skips OpenVR and publishes a synthetic gaze, so the
# running path is tested without SteamVR: then "running" + "parent exited" is required, and
# the log's sample count must be over 100.
# With -Overlay the helper runs as the DLL starts it (overlay app); if SteamVR isn't running it
# must fail without starting any SteamVR process (no child processes left behind).
# With -Hang the helper blocks forever where VR init would be (--fake-hang); its parent watchdog
# must still end it within 1 s of the parent exiting.
param([Parameter(Mandatory = $true)][string]$Helper, [switch]$Fake, [switch]$Overlay, [switch]$Hang)

if (-not (Test-Path $Helper)) { Write-Output "FAIL: no helper at $Helper"; exit 1 }
$log = Join-Path (Split-Path $Helper) 'rez_gaze_helper.log'
Remove-Item $log -ErrorAction SilentlyContinue

# Parent: lives about 3 s.
$parent = Start-Process -PassThru -WindowStyle Hidden -FilePath ping.exe -ArgumentList '-n', '4', '127.0.0.1'
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Helper
$psi.Arguments = "--parent $($parent.Id)" + $(if ($Overlay) { '' } else { ' --app background' }) +
                 $(if ($Fake) { ' --fake' } else { '' }) + $(if ($Hang) { ' --fake-hang' } else { '' })
$psi.UseShellExecute = $false  # a shell-launched process reports no ExitCode
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$parent.WaitForExit()
if (-not $proc.WaitForExit(1000)) {
    $proc.Kill()
    Write-Output "FAIL: helper still running 1 s after its parent exited"
    exit 1
}
# Only processes created while the helper lived: once it exits, Windows reuses its PID, and an
# unrelated process can carry that number as its parent id (a false alarm seen 2026-10-24).
$born = $proc.StartTime
$died = $proc.ExitTime
$children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($proc.Id)" |
    Where-Object { $_.CreationDate -ge $born -and $_.CreationDate -le $died })
if ($children.Count -gt 0) {
    Write-Output "FAIL: the helper left child processes: $(($children | ForEach-Object Name) -join ', ')"
    exit 1
}
$text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
Write-Output "helper exit code $($proc.ExitCode)"
Write-Output $text
if ($Hang) {
    if ($text -match 'watchdog') { Write-Output 'PASS (hang)'; exit 0 }
    Write-Output 'FAIL: hang run lacks the watchdog line'
    exit 1
}
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
