#Requires -Version 5
<#
    Standalone, auditable installer. Downloads and validates one release BEFORE
    stopping Spotify. Replaces files atomically and rolls back ordinary failures.
    Dot-source to load functions without installing (used by isolated tests).
#>

function Test-BtsDll([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $pe = $reader.ReadUInt32()
        if ($pe -gt $stream.Length - 24) { throw "Invalid PE header: $Path" }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Not an x64 PE: $Path" }
        $stream.Position = $pe + 22
        if (-not ($reader.ReadUInt16() -band 0x2000)) { throw "Not a DLL: $Path" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Test-BtsGenuineDll([string]$Path, [string]$SpotifyDirectory) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Spotify (AB|Ltd)') { return $false }
    Test-BtsDll $Path
    $cef = (Get-Item -LiteralPath (Join-Path $SpotifyDirectory 'libcef.dll')).VersionInfo.FileVersion
    if ($cef -notmatch 'chromium-(\d+\.\d+\.\d+\.\d+)') { throw 'Cannot determine the installed Chromium version; refusing to guess which backup is compatible.' }
    $expected = $Matches[1]
    $version = (Get-Item -LiteralPath $Path).VersionInfo.FileVersion
    return $version -eq $expected
}

function Get-BtsRelease([string]$Repo) {
    Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ 'User-Agent' = 'BlockTheSpot-Resilient' }
}

function Receive-BtsAsset([string]$Url, [string]$Path) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try { Invoke-WebRequest $Url -OutFile $Path -UseBasicParsing; return }
        catch { if ($attempt -eq 3) { throw }; Start-Sleep -Seconds (2 * $attempt) }
    }
}

function Set-BtsFile([string]$Source, [string]$Destination) {
    # Source is staged on the same volume. Never truncate the live destination.
    if ([IO.File]::Exists($Destination)) { [IO.File]::Replace($Source, $Destination, [NullString]::Value) }
    else { [IO.File]::Move($Source, $Destination) }
}

function Stop-BtsSpotify([string]$Directory) {
    $exe = Join-Path $Directory 'Spotify.exe'
    $processes = @(Get-Process Spotify -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
    foreach ($process in $processes) { $process | Stop-Process -Force -ErrorAction Stop }
    foreach ($process in $processes) { if (-not $process.WaitForExit(10000)) { throw 'Spotify did not stop; no files replaced.' } }
}

function Start-BtsSpotify([string]$Directory) {
    Start-Process -FilePath (Join-Path $Directory 'Spotify.exe') -WorkingDirectory $Directory
}

function Install-BlockTheSpot {
    param([string]$SpotifyDirectory = (Join-Path $env:APPDATA 'Spotify'), [switch]$NoLaunch)
    $ErrorActionPreference = 'Stop'
    $Repo = 'thomas-quant/BlockTheSpot-Resilient'
    $SpotifyDirectory = [IO.Path]::GetFullPath($SpotifyDirectory)
    if (-not (Test-Path -LiteralPath (Join-Path $SpotifyDirectory 'Spotify.exe') -PathType Leaf)) {
        throw "Standalone Spotify not found at $SpotifyDirectory (Microsoft Store installs are unsupported)."
    }
    # Also used by the uninstaller. The OS releases this lock after a crash.
    $lock = [IO.File]::Open((Join-Path $SpotifyDirectory '.blockthespot.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $stage = Join-Path $SpotifyDirectory ('.blockthespot-stage-' + [guid]::NewGuid().ToString('N'))
    $keepStage = $false
    $committed = $false
    try {
        $incoming = New-Item -ItemType Directory -Path (Join-Path $stage 'incoming') -Force
        $previous = New-Item -ItemType Directory -Path (Join-Path $stage 'previous') -Force
        $release = Get-BtsRelease $Repo
        if (-not $release.tag_name -or $release.draft -or $release.prerelease) { throw 'No stable release available' }
        $prefix = "https://github.com/$Repo/releases/download/$([uri]::EscapeDataString($release.tag_name))/"
        foreach ($name in 'chrome_elf.dll', 'blockthespot.dll', 'config.ini') {
            $assets = @($release.assets | Where-Object { $_.name -ceq $name })
            if ($assets.Count -ne 1) { throw "Release is missing a unique $name asset" }
            $asset = $assets[0]
            if ($asset.digest -notmatch '^sha256:([a-fA-F0-9]{64})$') { throw "No SHA-256 digest for $name; refusing an unverified download" }
            $expectedHash = $Matches[1]
            if ($asset.browser_download_url -cne ($prefix + $name)) { throw "Unexpected release URL for $name" }
            $file = Join-Path $incoming.FullName $name
            Write-Host "Downloading $name from $($release.tag_name)..." -ForegroundColor Cyan
            Receive-BtsAsset $asset.browser_download_url $file
            if ((Get-Item -LiteralPath $file).Length -ne $asset.size -or
                (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $expectedHash) { throw "Size/hash mismatch for $name" }
            if ($name.EndsWith('.dll')) { Test-BtsDll $file }
        }
        $configText = Get-Content -LiteralPath (Join-Path $incoming.FullName 'config.ini') -Raw
        foreach ($section in 'LIBCEF', 'URL_block', 'Buffer_modify') {
            if ($configText -notmatch "(?m)^\[$section\]\s*$") { throw "Downloaded config is missing [$section]" }
        }
        $chrome = Join-Path $SpotifyDirectory 'chrome_elf.dll'
        $backup = Join-Path $SpotifyDirectory 'chrome_elf_required.dll'
        if (-not (Test-BtsGenuineDll $chrome $SpotifyDirectory) -and
            -not (Test-BtsGenuineDll $backup $SpotifyDirectory)) { throw 'No signed, version-compatible original chrome_elf.dll; repair Spotify first. Nothing replaced.' }

        # Stop only this installation, after the network work has succeeded.
        Stop-BtsSpotify $SpotifyDirectory
        $original = if (Test-BtsGenuineDll $chrome $SpotifyDirectory) { $chrome } else { $backup }
        if (-not (Test-BtsGenuineDll $original $SpotifyDirectory)) { throw 'Spotify changed during installation; refusing to use a stale backup.' }
        $order = @('chrome_elf_required.dll', 'blockthespot.dll', 'config.ini', 'chrome_elf.dll')
        Copy-Item -LiteralPath $original -Destination (Join-Path $incoming.FullName 'chrome_elf_required.dll')
        if (Test-Path -LiteralPath (Join-Path $SpotifyDirectory 'config.ini') -PathType Leaf) {
            Copy-Item -LiteralPath (Join-Path $SpotifyDirectory 'config.ini') -Destination (Join-Path $incoming.FullName 'config.ini.previous')
            $order = @('config.ini.previous') + $order
        }
        $existed = @{}
        foreach ($name in $order) {
            $path = Join-Path $SpotifyDirectory $name
            $existed[$name] = Test-Path -LiteralPath $path -PathType Leaf
            if ($existed[$name]) { Copy-Item -LiteralPath $path -Destination (Join-Path $previous.FullName $name) }
        }
        # Journal remains with snapshots if rollback itself fails or the process
        # is forcibly terminated. This is not a multi-file power-loss transaction.
        $existed | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'prior-state.json') -Encoding UTF8
        $attempted = New-Object 'Collections.Generic.List[string]'
        try {
            foreach ($name in $order) {
                $attempted.Add($name)
                Set-BtsFile (Join-Path $incoming.FullName $name) (Join-Path $SpotifyDirectory $name)
            }
            $committed = $true
        } catch {
            $failure = $_
            $failedRestore = @()
            for ($i = $attempted.Count - 1; $i -ge 0; $i--) {
                $name = $attempted[$i]
                try {
                    $destination = Join-Path $SpotifyDirectory $name
                    if ($existed[$name]) {
                        $restore = Join-Path $stage ('restore-' + $name)
                        Copy-Item -LiteralPath (Join-Path $previous.FullName $name) -Destination $restore -Force
                        Set-BtsFile $restore $destination
                    } elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
                } catch { $failedRestore += $name }
            }
            if ($failedRestore.Count) {
                $keepStage = $true
                throw "Install failed ($failure); rollback incomplete for $($failedRestore -join ', '). Recovery snapshots: $stage. Do not launch Spotify until restored."
            }
            throw "Install failed; previous files restored: $failure"
        }
    } finally {
        if (-not $keepStage -and (Test-Path -LiteralPath $stage)) { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
        $lock.Dispose()
    }
    if ($committed) {
        Write-Host "Installed release $($release.tag_name). Previous settings, if present, are in config.ini.previous." -ForegroundColor Green
        if (-not $NoLaunch) {
            try { Start-BtsSpotify $SpotifyDirectory }
            catch { Write-Warning "Installation succeeded but launch failed: $_. Start Spotify manually." }
        }
    }
}

if ($MyInvocation.InvocationName -ne '.') { Install-BlockTheSpot }
