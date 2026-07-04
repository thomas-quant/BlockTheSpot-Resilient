#Requires -Version 5
<#
    BlockTheSpot-Resilient installer.

    Downloads the latest release DLLs and patches the Spotify desktop client
    in place. Everything it does is visible below - no compiled installer, no
    third-party mirrors, no signature stripping. Read it, then run it.

    Usage (from PowerShell):
        .\install.ps1
    or one-liner:
        iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/install.ps1 | iex
#>
$ErrorActionPreference = 'Stop'

$Repo    = 'thomas-quant/BlockTheSpot-Resilient'
$Spotify = Join-Path $env:APPDATA 'Spotify'
$Exe     = Join-Path $Spotify 'Spotify.exe'
$Chrome  = Join-Path $Spotify 'chrome_elf.dll'
$Backup  = Join-Path $Spotify 'chrome_elf_required.dll'

if (-not (Test-Path $Exe)) {
    throw "Spotify not found at $Spotify. Install the standalone Spotify app (not the Microsoft Store version) first."
}

Write-Host 'Stopping Spotify...' -ForegroundColor Cyan
Get-Process Spotify -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Back up the genuine chrome_elf.dll so the forwarder can load it as
# chrome_elf_required.dll. The genuine Chromium DLL has version info; our
# forwarder has none - so we only ever back up the real one, and this also
# refreshes the backup after Spotify replaces chrome_elf.dll on an update.
if (Test-Path $Chrome) {
    $ver = (Get-Item $Chrome).VersionInfo.FileVersion
    if ($ver) {
        Copy-Item $Chrome $Backup -Force
        Write-Host "Backed up genuine chrome_elf.dll ($ver)" -ForegroundColor Cyan
    }
}
if (-not (Test-Path $Backup)) {
    throw 'No genuine chrome_elf.dll found to back up - aborting so nothing is broken.'
}

# Fetch the latest release artifacts and drop them in.
$Base = "https://github.com/$Repo/releases/latest/download"
foreach ($f in 'chrome_elf.dll', 'blockthespot.dll', 'config.ini') {
    Write-Host "Downloading $f..." -ForegroundColor Cyan
    Invoke-WebRequest "$Base/$f" -OutFile (Join-Path $Spotify $f) -UseBasicParsing
}

Write-Host 'Patched. Launching Spotify...' -ForegroundColor Green
Start-Process -FilePath $Exe -WorkingDirectory $Spotify
