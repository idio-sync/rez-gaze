# Shared by install.ps1 and uninstall.ps1.

# SHA-256 of Rez Infinite's own REZ\openvr_api.dll (302,880 bytes, 2021; Steam build 2.8).
$RezOpenVrHash = '74eeb0c989d10e8aaf20da5ae2bc1fa7dd34e74fd3c54f9cce40544878e4e157'

function Fail([string]$message) {
    Write-Host "rez-gaze: $message" -ForegroundColor Red
    exit 1
}

function Sha256([string]$path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }

# Rez Infinite's REZ folder, from Steam's library list, or $null.
function Find-RezDir {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $m.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($library in $libraries) {
        $dir = Join-Path $library 'steamapps\common\Rez Infinite\REZ'
        if (Test-Path -LiteralPath (Join-Path $dir 'REZ.exe')) { return $dir }
    }
    return $null
}

function Resolve-RezDir([string]$given) {
    $dir = if ($given) { $given } else { Find-RezDir }
    if (-not $dir) { Fail "couldn't find Rez Infinite; pass -RezDir '<...>\Rez Infinite\REZ'" }
    if (-not (Test-Path -LiteralPath (Join-Path $dir 'REZ.exe'))) { Fail "no REZ.exe in $dir (it's the REZ folder inside Rez Infinite)" }
    return $dir
}

function Assert-RezClosed {
    $running = Get-Process -Name 'REZ', 'REZ_120', 'Rez-infinite' -ErrorAction SilentlyContinue
    if ($running) { Fail 'Rez Infinite is running; close it first' }
}
