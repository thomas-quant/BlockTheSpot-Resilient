# No network, installed Spotify access, or process control. All files are fixtures.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../install.ps1')
. (Join-Path $PSScriptRoot '../uninstall.ps1')
$script:Checks = 0
function Assert-Bts($Condition, $Message) {
    $script:Checks++
    if (-not $Condition) { throw "ASSERT: $Message" }
}
function Expect-Failure([scriptblock]$Action, [string]$Pattern) {
    $failed = $false
    try { & $Action } catch {
        $failed = $true
        Assert-Bts ($_.Exception.Message -match $Pattern) "Unexpected failure: $_"
    }
    Assert-Bts $failed 'Expected an error'
}
function New-FixtureDll([string]$Path, [string]$Marker, [int]$Machine = 0x8664) {
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4d; $bytes[1] = 0x5a; $bytes[0x3c] = 0x40
    $bytes[0x40] = 0x50; $bytes[0x41] = 0x45
    [BitConverter]::GetBytes([uint16]$Machine).CopyTo($bytes, 0x44)
    $bytes[0x57] = 0x20
    [Text.Encoding]::ASCII.GetBytes($Marker).CopyTo($bytes, 128)
    [IO.File]::WriteAllBytes($Path, $bytes)
}
function Is-FixtureOriginal([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path)).Contains('genuine-current')
}
# Replace ONLY external boundaries. PE parsing, hashing, file replacement,
# snapshotting and rollback remain the real production implementations.
function Test-BtsGenuineDll($Path, $Directory) { Is-FixtureOriginal $Path }
function Test-BtsRestoreDll($Path, $Directory) { Is-FixtureOriginal $Path }
function Stop-BtsSpotify($Directory) { $script:Stops++ }
function Stop-BtsUninstallSpotify($Directory) { $script:Stops++ }
function Start-BtsSpotify($Directory) { $script:Starts++ }
function Get-BtsRelease($Repo) { $script:Release }
function Receive-BtsAsset($Url, $Path) {
    $name = $Url.Split('/')[-1]
    if ($name -eq $script:DownloadFailure) { throw 'injected download failure' }
    Copy-Item -LiteralPath (Join-Path $script:Assets $name) -Destination $Path
}
$script:RealSetFile = ${function:Set-BtsFile}
function Set-BtsFile($Source, $Destination) {
    $script:Writes++
    if ($script:Writes -eq $script:WriteFailure) { throw 'injected replacement failure' }
    & $script:RealSetFile $Source $Destination
    if ($script:Writes -eq $script:FailAfterWrite) { throw 'injected failure after replacement' }
}
function Asset-Metadata {
    $items = foreach ($name in 'chrome_elf.dll', 'blockthespot.dll', 'config.ini') {
        $path = Join-Path $script:Assets $name
        [pscustomobject]@{ name = $name; size = (Get-Item -LiteralPath $path).Length;
            digest = 'sha256:' + (Get-FileHash -LiteralPath $path).Hash;
            browser_download_url = "https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/download/vfixture/$name" }
    }
    $script:Release = [pscustomobject]@{ tag_name = 'vfixture'; draft = $false; prerelease = $false; assets = @($items) }
}
function Reset-Fixture {
    if (Test-Path -LiteralPath $script:Spotify) { Remove-Item -LiteralPath $script:Spotify -Recurse -Force }
    New-Item -ItemType Directory -Path $script:Spotify | Out-Null
    [IO.File]::WriteAllText((Join-Path $script:Spotify 'Spotify.exe'), 'test only')
    New-FixtureDll (Join-Path $script:Spotify 'chrome_elf.dll') 'genuine-current'
    New-FixtureDll (Join-Path $script:Assets 'chrome_elf.dll') 'proxy-new'
    New-FixtureDll (Join-Path $script:Assets 'blockthespot.dll') 'payload-new'
    [IO.File]::WriteAllText((Join-Path $script:Assets 'config.ini'), "[LIBCEF]`r`n[URL_block]`r`nEnable=1`r`n[Buffer_modify]`r`nEnable=1`r`n")
    [IO.File]::WriteAllText((Join-Path $script:Spotify 'config.ini'), 'custom settings')
    $script:Stops = 0; $script:Starts = 0; $script:Writes = 0
    $script:DownloadFailure = ''; $script:WriteFailure = -1; $script:FailAfterWrite = -1
    Asset-Metadata
}
function Fingerprint {
    $files = Get-ChildItem -LiteralPath $script:Spotify -File | Where-Object { $_.Name -ne '.blockthespot.lock' } | Sort-Object Name
    return ($files | ForEach-Object { $_.Name + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash }) -join '|'
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('bts-installer-test-' + [guid]::NewGuid().ToString('N'))
# Exercise Unicode paths without depending on the script file's encoding in PS5.
$script:Spotify = Join-Path $root ('Spotify-' + [char]0x65e5 + [char]0x672c)
$script:Assets = (New-Item -ItemType Directory -Path (Join-Path $root 'assets') -Force).FullName
try {
    Reset-Fixture
    $before = Fingerprint
    $script:DownloadFailure = 'config.ini'
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'download failure'
    Assert-Bts ((Fingerprint) -eq $before -and $script:Stops -eq 0) 'Download failure touched/stopped Spotify'

    Reset-Fixture; $before = Fingerprint
    $corruptPath = Join-Path $script:Assets 'blockthespot.dll'
    $corrupt = [IO.File]::ReadAllBytes($corruptPath); $corrupt[511] = 1
    [IO.File]::WriteAllBytes($corruptPath, $corrupt) # same size, wrong digest
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'hash mismatch'
    Assert-Bts ((Fingerprint) -eq $before -and $script:Stops -eq 0) 'Bad hash touched/stopped Spotify'

    Reset-Fixture; $before = Fingerprint
    New-FixtureDll (Join-Path $script:Assets 'blockthespot.dll') 'wrong-architecture' 0x14c
    Asset-Metadata
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'Not an x64'
    Assert-Bts ((Fingerprint) -eq $before) 'Wrong-architecture asset changed live files'

    Reset-Fixture; $before = Fingerprint
    $script:Release.assets[0].digest = $null
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'No SHA-256'
    $script:Release.assets[0].digest = 'sha256:' + ('0' * 64)
    $script:Release.assets[0].browser_download_url = 'https://github.com/other/releases/download/vfixture/chrome_elf.dll'
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'Unexpected release URL'
    Assert-Bts ((Fingerprint) -eq $before -and $script:Stops -eq 0) 'Invalid metadata changed live state'

    Reset-Fixture
    Install-BlockTheSpot $script:Spotify
    Assert-Bts ($script:Stops -eq 1 -and $script:Starts -eq 1) 'Successful install did not stop/start once'
    Assert-Bts (Is-FixtureOriginal (Join-Path $script:Spotify 'chrome_elf_required.dll')) 'Genuine backup not retained'
    Assert-Bts ((Get-Content -LiteralPath (Join-Path $script:Spotify 'config.ini.previous') -Raw) -eq 'custom settings') 'Custom settings not saved'
    Assert-Bts (-not (Is-FixtureOriginal (Join-Path $script:Spotify 'chrome_elf.dll'))) 'Proxy not installed'
    # Repeat installation must not back up the proxy as the genuine DLL.
    Install-BlockTheSpot $script:Spotify -NoLaunch
    Assert-Bts (Is-FixtureOriginal (Join-Path $script:Spotify 'chrome_elf_required.dll')) 'Reinstall backed up the proxy'

    $before = Fingerprint
    foreach ($position in 1..5) {
        $script:Writes = 0; $script:WriteFailure = $position
        Expect-Failure { Install-BlockTheSpot $script:Spotify } 'previous files restored'
        Assert-Bts ((Fingerprint) -eq $before) "Failure at replacement $position did not restore exact previous state"
    }
    $script:WriteFailure = -1; $script:Writes = 0; $script:FailAfterWrite = 5
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'previous files restored'
    Assert-Bts ((Fingerprint) -eq $before) 'Failure after proxy replacement did not roll back'
    $script:FailAfterWrite = -1
    $locked = [IO.File]::Open((Join-Path $script:Spotify 'blockthespot.dll'), 'Open', 'Read', 'Read')
    try { Expect-Failure { Install-BlockTheSpot $script:Spotify } 'rollback incomplete' }
    finally { $locked.Dispose() }
    Assert-Bts ((Fingerprint) -eq $before) 'Locked target changed old files'
    Assert-Bts (@(Get-ChildItem -LiteralPath $script:Spotify -Directory -Filter '.blockthespot-stage-*').Count -eq 1) 'Failed rollback did not retain recovery snapshots'

    Reset-Fixture
    $lock = [IO.File]::Open((Join-Path $script:Spotify '.blockthespot.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        Expect-Failure { Install-BlockTheSpot $script:Spotify } 'used by another process|being used'
        Expect-Failure { Uninstall-BlockTheSpot $script:Spotify } 'used by another process|being used'
    } finally { $lock.Dispose() }
    Assert-Bts ($script:Stops -eq 0) 'Concurrent installation proceeded'

    Reset-Fixture; Install-BlockTheSpot $script:Spotify -NoLaunch
    Remove-Item -LiteralPath (Join-Path $script:Spotify 'chrome_elf_required.dll')
    $before = Fingerprint
    Expect-Failure { Install-BlockTheSpot $script:Spotify } 'No signed, version-compatible'
    Expect-Failure { Uninstall-BlockTheSpot $script:Spotify } 'Nothing removed'
    Assert-Bts ((Fingerprint) -eq $before) 'Missing backup caused destructive cleanup'

    New-FixtureDll (Join-Path $script:Spotify 'chrome_elf_required.dll') 'genuine-stale'
    $before = Fingerprint
    Expect-Failure { Uninstall-BlockTheSpot $script:Spotify } 'Nothing removed'
    Assert-Bts ((Fingerprint) -eq $before) 'Stale backup replaced live proxy'
    # A Spotify update replaced the proxy already: keep the newer genuine DLL.
    New-FixtureDll (Join-Path $script:Spotify 'chrome_elf.dll') 'genuine-current'
    $hash = (Get-FileHash -LiteralPath (Join-Path $script:Spotify 'chrome_elf.dll')).Hash
    Uninstall-BlockTheSpot $script:Spotify
    Assert-Bts ((Get-FileHash -LiteralPath (Join-Path $script:Spotify 'chrome_elf.dll')).Hash -eq $hash) 'Uninstall downgraded a genuine DLL'
    Assert-Bts (-not (Test-Path -LiteralPath (Join-Path $script:Spotify 'blockthespot.dll'))) 'Payload not removed after safe restore'

    Reset-Fixture; Install-BlockTheSpot $script:Spotify -NoLaunch
    Uninstall-BlockTheSpot $script:Spotify
    Assert-Bts (Is-FixtureOriginal (Join-Path $script:Spotify 'chrome_elf.dll')) 'Uninstall did not restore genuine DLL'
    Assert-Bts (Test-Path -LiteralPath (Join-Path $script:Spotify 'config.ini.previous')) 'Uninstall discarded preserved settings'
    Write-Output "PASS: $script:Checks installer/uninstaller checks using isolated Unicode-path fixtures."
} finally { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
