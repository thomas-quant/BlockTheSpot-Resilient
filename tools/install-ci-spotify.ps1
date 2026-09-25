# This script intentionally modifies ONLY a disposable GitHub-hosted runner.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Spotify CI installation is restricted to disposable GitHub-hosted runners.'
}
$setup = Join-Path $env:RUNNER_TEMP 'SpotifyFullSetupX64.exe'
Invoke-WebRequest 'https://download.scdn.co/SpotifyFullSetupX64.exe' -OutFile $setup
$signature = Get-AuthenticodeSignature $setup
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Spotify (AB|Ltd)') {
    throw 'Official Spotify installer signature verification failed'
}
$installer = Start-Process $setup -ArgumentList '/silent' -PassThru
$spotify = Join-Path $env:APPDATA 'Spotify'
$exe = Join-Path $spotify 'Spotify.exe'
$spa = Join-Path $spotify 'Apps/xpui.spa'
$deadline = (Get-Date).AddMinutes(8)
while ((-not (Test-Path $exe) -or -not (Test-Path $spa)) -and (Get-Date) -lt $deadline) { Start-Sleep 5 }
if (-not (Test-Path $exe) -or -not (Test-Path $spa)) { throw 'Spotify installation timed out' }
if (-not $installer.WaitForExit(120000)) { throw 'Spotify installer did not exit' }
Start-Sleep 5
Get-Process Spotify -ErrorAction SilentlyContinue | Stop-Process -Force
$version = (Get-Item $exe).VersionInfo.FileVersion
Write-Host "Installed Spotify version: $version"
"version=$version" >> $env:GITHUB_OUTPUT
"spotify=$spotify" >> $env:GITHUB_OUTPUT
"spa=$spa" >> $env:GITHUB_OUTPUT
