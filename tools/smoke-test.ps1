param(
    [Parameter(Mandatory)][string]$SpotifyDirectory,
    [string]$Dist = 'dist',
    [string]$ReportDirectory = 'reports'
)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Runtime smoke testing is restricted to disposable GitHub-hosted runners.'
}
New-Item -ItemType Directory -Force $ReportDirectory | Out-Null
$log = Join-Path $SpotifyDirectory 'blockthespot.log'
$exe = Join-Path $SpotifyDirectory 'Spotify.exe'
$version = (Get-Item $exe).VersionInfo.FileVersion
try {
    Get-Process Spotify -ErrorAction SilentlyContinue | Stop-Process -Force
    Copy-Item (Join-Path $SpotifyDirectory 'chrome_elf.dll') (Join-Path $SpotifyDirectory 'chrome_elf_required.dll') -Force
    foreach ($file in 'chrome_elf.dll', 'blockthespot.dll', 'config.ini') {
        Copy-Item (Join-Path $Dist $file) (Join-Path $SpotifyDirectory $file) -Force
    }
    # Information-level diagnostics contain no request URLs or account data.
    $config = Join-Path $SpotifyDirectory 'config.ini'
    (Get-Content $config -Raw) -replace '(?m)^Level=\d+', 'Level=1' | Set-Content $config -Encoding ascii
    Remove-Item $log -ErrorAction SilentlyContinue
    Start-Process $exe -WorkingDirectory $SpotifyDirectory | Out-Null
    $deadline = (Get-Date).AddSeconds(90)
    $text = ''
    do {
        Start-Sleep 2
        if (Test-Path $log) { $text = Get-Content $log -Raw }
        $ready = $text -match 'CEF hook installation: OK' -and
            $text -match 'CEF URL handler active \(URL decoded\)' -and
            $text -match 'CEF ZIP read callback observed \(filename decoded\)'
    } until ($ready -or (Get-Date) -gt $deadline)
    if (-not $ready) { throw 'Missing hook installation or actual URL/ZIP callback evidence; inspect blockthespot.log' }
    Start-Sleep 10
    if (-not (Get-Process Spotify -ErrorAction SilentlyContinue)) { throw 'Spotify exited after hook installation' }
    @"
PASS: Spotify $version starts with the built DLLs, imports are intercepted, and URL/ZIP callbacks execute with decoded strings.
This is a logged-out startup smoke test. It does NOT verify ad-free playback, signed-in UI behavior, or every future CEF ABI.
"@ | Set-Content (Join-Path $ReportDirectory 'smoke.txt')
    Get-Content (Join-Path $ReportDirectory 'smoke.txt') | Write-Host
} catch {
    "FAIL: Spotify $version smoke test: $_" | Set-Content (Join-Path $ReportDirectory 'smoke.txt')
    throw
} finally {
    if (Test-Path $log) {
        Copy-Item $log (Join-Path $ReportDirectory 'blockthespot.log') -Force
        Get-Content $log | Write-Host
    }
    Get-Process Spotify -ErrorAction SilentlyContinue | Stop-Process -Force
}
