#Requires -Version 5
<#
    Restore the genuine DLL BEFORE removing the payload. A missing/stale backup
    must not turn an otherwise working patched installation into a broken one.
    Self-contained for the documented iwr | iex usage; dot-source for tests.
#>
function Test-BtsRestoreDll([string]$Path, [string]$Directory) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Spotify (AB|Ltd)') { return $false }
    $cef = (Get-Item -LiteralPath (Join-Path $Directory 'libcef.dll')).VersionInfo.FileVersion
    if ($cef -notmatch 'chromium-(\d+\.\d+\.\d+\.\d+)') { throw 'Cannot establish backup compatibility; repair Spotify before uninstalling.' }
    return (Get-Item -LiteralPath $Path).VersionInfo.FileVersion -eq $Matches[1]
}

function Stop-BtsUninstallSpotify([string]$Directory) {
    $exe = Join-Path $Directory 'Spotify.exe'
    $processes = @(Get-Process Spotify -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
    foreach ($process in $processes) { $process | Stop-Process -Force -ErrorAction Stop }
    foreach ($process in $processes) { if (-not $process.WaitForExit(10000)) { throw 'Spotify did not stop; no files removed.' } }
}

function Uninstall-BlockTheSpot {
    param([string]$SpotifyDirectory = (Join-Path $env:APPDATA 'Spotify'))
    $ErrorActionPreference = 'Stop'
    $SpotifyDirectory = [IO.Path]::GetFullPath($SpotifyDirectory)
    $chrome = Join-Path $SpotifyDirectory 'chrome_elf.dll'
    $backup = Join-Path $SpotifyDirectory 'chrome_elf_required.dll'
    $lock = [IO.File]::Open((Join-Path $SpotifyDirectory '.blockthespot.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $temporary = Join-Path $SpotifyDirectory ('.blockthespot-restore-' + [guid]::NewGuid().ToString('N'))
    try {
        if (-not (Test-BtsRestoreDll $chrome $SpotifyDirectory) -and
            -not (Test-BtsRestoreDll $backup $SpotifyDirectory)) {
            throw 'No signed, version-compatible original DLL. Nothing removed; repair Spotify before uninstalling.'
        }
        Stop-BtsUninstallSpotify $SpotifyDirectory
        # Spotify may already have replaced the proxy during an update. Never
        # overwrite that newer genuine DLL with an older backup.
        if (-not (Test-BtsRestoreDll $chrome $SpotifyDirectory)) {
            if (-not (Test-BtsRestoreDll $backup $SpotifyDirectory)) { throw 'Backup changed or is stale; nothing removed.' }
            Copy-Item -LiteralPath $backup -Destination $temporary
            if ([IO.File]::Exists($chrome)) { [IO.File]::Replace($temporary, $chrome, [NullString]::Value) }
            else { [IO.File]::Move($temporary, $chrome) }
        }
        if (-not (Test-BtsRestoreDll $chrome $SpotifyDirectory)) { throw 'Original DLL restoration could not be verified; leaving payload and config intact.' }
        foreach ($name in 'blockthespot.dll', 'config.ini') {
            $path = Join-Path $SpotifyDirectory $name
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
        }
        Write-Host 'Uninstalled. Genuine chrome_elf.dll restored; backup and previous settings retained. Relaunch Spotify normally.' -ForegroundColor Green
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue }
        $lock.Dispose()
    }
}

if ($MyInvocation.InvocationName -ne '.') { Uninstall-BlockTheSpot }
