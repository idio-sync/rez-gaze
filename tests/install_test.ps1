# Tests release/install.ps1 and release/uninstall.ps1 against a fake REZ folder and a fake
# package, never the real game. Run by CTest (test "installer").
param([Parameter(Mandatory = $true)][string]$Release)

$ErrorActionPreference = 'Stop'
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("rez_gaze_install_test_" + [guid]::NewGuid().ToString('N'))
$fail = 0
function Check($cond, $what) {
    if ($cond) { Write-Output "PASS $what" } else { Write-Output "FAIL $what"; $script:fail++ }
}
function Content($path) { if (Test-Path $path) { [IO.File]::ReadAllText($path) } else { '<missing>' } }
function Hash($text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    ($sha.ComputeHash([Text.Encoding]::ASCII.GetBytes($text)) | ForEach-Object { $_.ToString('x2') }) -join ''
}
function NewPackage($dir, $standin, $ini) {
    New-Item -ItemType Directory -Force (Join-Path $dir 'rez_gaze') | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'openvr_api.dll'), $standin)
    [IO.File]::WriteAllText((Join-Path $dir 'rez_gaze\rez_gaze_helper.exe'), 'HELPER')
    [IO.File]::WriteAllText((Join-Path $dir 'rez_gaze\openvr_api.dll'), 'OPENVR2156')
    [IO.File]::WriteAllText((Join-Path $dir 'rez_gaze.ini'), $ini)
}
function Install($pkg, $rez) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Release 'install.ps1') -PackageDir $pkg -RezDir $rez -OriginalHash (Hash 'ORIGINAL') | Out-Null
    $LASTEXITCODE
}
function Uninstall($rez) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Release 'uninstall.ps1') -RezDir $rez -OriginalHash (Hash 'ORIGINAL') | Out-Null
    $LASTEXITCODE
}

try {
    $rez = Join-Path $root 'REZ'
    $pkg = Join-Path $root 'pkg'
    New-Item -ItemType Directory -Force $rez | Out-Null
    [IO.File]::WriteAllText((Join-Path $rez 'REZ.exe'), 'GAME')
    [IO.File]::WriteAllText((Join-Path $rez 'openvr_api.dll'), 'ORIGINAL')
    NewPackage $pkg 'STANDIN1' "[rez_gaze]`r`nmode=gaze`r`n"

    # 1. Fresh install
    Check ((Install $pkg $rez) -eq 0) 'fresh install exits 0'
    Check ((Content "$rez\openvr_api_real.dll") -eq 'ORIGINAL') 'original kept as openvr_api_real.dll'
    Check ((Content "$rez\openvr_api.dll") -eq 'STANDIN1') 'stand-in installed'
    Check ((Content "$rez\rez_gaze\rez_gaze_helper.exe") -eq 'HELPER') 'helper installed'
    Check ((Content "$rez\rez_gaze\openvr_api.dll") -eq 'OPENVR2156') 'helper runtime installed'
    Check (Test-Path "$rez\rez_gaze.ini") 'default ini installed'

    # 2. Update keeps the user's ini
    [IO.File]::WriteAllText((Join-Path $rez 'rez_gaze.ini'), 'MY SETTINGS')
    NewPackage $pkg 'STANDIN2' 'NEW DEFAULTS'
    Check ((Install $pkg $rez) -eq 0) 'update exits 0'
    Check ((Content "$rez\openvr_api.dll") -eq 'STANDIN2') 'update replaces the stand-in'
    Check ((Content "$rez\openvr_api_real.dll") -eq 'ORIGINAL') 'update keeps the original'
    Check ((Content "$rez\rez_gaze.ini") -eq 'MY SETTINGS') "update keeps the user's ini"

    # 3. Unknown openvr_api.dll: refuse and change nothing
    $rez2 = Join-Path $root 'REZ2'
    New-Item -ItemType Directory -Force $rez2 | Out-Null
    [IO.File]::WriteAllText((Join-Path $rez2 'REZ.exe'), 'GAME')
    [IO.File]::WriteAllText((Join-Path $rez2 'openvr_api.dll'), 'SOMETHING ELSE')
    Check ((Install $pkg $rez2) -ne 0) 'unknown openvr_api.dll: install refuses'
    Check ((Content "$rez2\openvr_api.dll") -eq 'SOMETHING ELSE') 'unknown openvr_api.dll left alone'
    Check (-not (Test-Path "$rez2\openvr_api_real.dll")) 'nothing renamed'

    # 4. A running REZ.exe: refuse (a copy of ping.exe named REZ.exe stands in for the game)
    $fakeDir = Join-Path $root 'fakegame'
    New-Item -ItemType Directory -Force $fakeDir | Out-Null
    Copy-Item "$env:SystemRoot\System32\PING.EXE" (Join-Path $fakeDir 'REZ.exe')
    $game = Start-Process -PassThru -WindowStyle Hidden (Join-Path $fakeDir 'REZ.exe') -ArgumentList '-n', '6', '127.0.0.1'
    Start-Sleep -Milliseconds 300
    NewPackage $pkg 'STANDIN3' 'X'
    Check ((Install $pkg $rez) -ne 0) 'install refuses while REZ.exe runs'
    Check ((Content "$rez\openvr_api.dll") -eq 'STANDIN2') 'nothing changed while REZ.exe runs'
    Check ((Uninstall $rez) -ne 0) 'uninstall refuses while REZ.exe runs'
    $game.Kill()
    $game.WaitForExit()

    # 5. Uninstall restores the original
    Check ((Uninstall $rez) -eq 0) 'uninstall exits 0'
    Check ((Content "$rez\openvr_api.dll") -eq 'ORIGINAL') 'original restored'
    Check (-not (Test-Path "$rez\openvr_api_real.dll")) 'openvr_api_real.dll gone'
    Check (-not (Test-Path "$rez\rez_gaze")) 'rez_gaze folder removed'
} finally {
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($fail -gt 0) { Write-Output "$fail check(s) failed"; exit 1 }
Write-Output 'PASS: installer'
exit 0
