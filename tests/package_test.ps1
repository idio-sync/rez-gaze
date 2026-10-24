# Tests release/package.ps1: it must build a zip with exactly the files a user needs, laid out to
# match Rez's REZ folder. Run by CTest (test "package").
param([Parameter(Mandatory = $true)][string]$Release, [Parameter(Mandatory = $true)][string]$Out)

$ErrorActionPreference = 'Stop'
$dest = Join-Path ([System.IO.Path]::GetTempPath()) ("rez_gaze_package_test_" + [guid]::NewGuid().ToString('N'))
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Release 'package.ps1') -Out $Out -Version 'test-1' -Dest $dest
    if ($LASTEXITCODE -ne 0) { Write-Output 'FAIL: package.ps1 failed'; exit 1 }
    $zip = Join-Path $dest 'rez-gaze-test-1.zip'
    if (-not (Test-Path $zip)) { Write-Output "FAIL: no $zip"; exit 1 }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
    $names = @($archive.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName.Replace('\', '/') }) | Sort-Object
    $archive.Dispose()
    $want = @(
        'rez-gaze-test-1/LICENSE-OpenVR.txt',
        'rez-gaze-test-1/README.txt',
        'rez-gaze-test-1/VERSION.txt',
        'rez-gaze-test-1/common.ps1',
        'rez-gaze-test-1/install.ps1',
        'rez-gaze-test-1/openvr_api.dll',
        'rez-gaze-test-1/rez_gaze.ini',
        'rez-gaze-test-1/rez_gaze/openvr_api.dll',
        'rez-gaze-test-1/rez_gaze/rez_gaze_helper.exe',
        'rez-gaze-test-1/uninstall.ps1'
    ) | Sort-Object
    $diff = Compare-Object $want $names
    if ($diff) {
        Write-Output 'FAIL: zip contents differ (<= expected only, => zip only):'
        $diff | ForEach-Object { Write-Output "  $($_.SideIndicator) $($_.InputObject)" }
        exit 1
    }
    Write-Output 'PASS: package'
    exit 0
} finally {
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
}
