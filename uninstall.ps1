#Requires -Version 5
<#
    BlockTheSpot-Resilient uninstaller.
    Removes the patch files and restores the original chrome_elf.dll.

    Usage:
        .\uninstall.ps1
    or:
        iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/uninstall.ps1 | iex
#>
$ErrorActionPreference = 'Stop'

$Spotify = Join-Path $env:APPDATA 'Spotify'
$Chrome  = Join-Path $Spotify 'chrome_elf.dll'
$Backup  = Join-Path $Spotify 'chrome_elf_required.dll'

Write-Host 'Stopping Spotify...' -ForegroundColor Cyan
Get-Process Spotify -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

Remove-Item (Join-Path $Spotify 'blockthespot.dll') -ErrorAction SilentlyContinue
Remove-Item (Join-Path $Spotify 'config.ini') -ErrorAction SilentlyContinue

if (Test-Path $Backup) {
    Move-Item $Backup $Chrome -Force
    Write-Host 'Restored original chrome_elf.dll.' -ForegroundColor Green
} else {
    Write-Host 'No backup found; left chrome_elf.dll as-is.' -ForegroundColor Yellow
}

Write-Host 'Uninstalled. Relaunch Spotify normally.' -ForegroundColor Green
