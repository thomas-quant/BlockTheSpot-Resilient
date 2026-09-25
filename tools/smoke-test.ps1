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
$chrome = Join-Path $SpotifyDirectory 'chrome_elf.dll'
$version = (Get-Item $exe).VersionInfo.FileVersion
$originalHash = (Get-FileHash $chrome).Hash
# Exercise the REAL installer, including genuine-DLL validation and transactions.
# Substitute only the release API/download boundary with this run's built files;
# branch verification must never publish a release just to test installation.
. (Join-Path $PSScriptRoot '../install.ps1')
. (Join-Path $PSScriptRoot '../uninstall.ps1')
$script:CIBuildAssets = (Resolve-Path $Dist).Path
$assets = foreach ($name in 'chrome_elf.dll', 'blockthespot.dll', 'config.ini') {
    $file = Join-Path $script:CIBuildAssets $name
    [pscustomobject]@{ name = $name; size = (Get-Item $file).Length; digest = 'sha256:' + (Get-FileHash $file).Hash;
        browser_download_url = "https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/download/ci-$env:GITHUB_SHA/$name" }
}
$script:CIBuildRelease = [pscustomobject]@{ tag_name = "ci-$env:GITHUB_SHA"; draft = $false; prerelease = $false; assets = @($assets) }
function Get-BtsRelease($Repo) { $script:CIBuildRelease }
function Receive-BtsAsset($Url, $Path) { Copy-Item -LiteralPath (Join-Path $script:CIBuildAssets $Url.Split('/')[-1]) -Destination $Path }
try {
    Install-BlockTheSpot -SpotifyDirectory $SpotifyDirectory -NoLaunch
    Install-BlockTheSpot -SpotifyDirectory $SpotifyDirectory -NoLaunch # never back up the proxy
    if ((Get-FileHash (Join-Path $SpotifyDirectory 'chrome_elf_required.dll')).Hash -ne $originalHash) {
        throw 'Reinstall changed the genuine backup'
    }
    Remove-Item $log -ErrorAction SilentlyContinue
    # Shortcuts, shells and launchers need not start in the Spotify directory.
    $unrelated = New-Item -ItemType Directory -Force (Join-Path $env:RUNNER_TEMP 'unrelated-working-directory')
    Start-Process $exe -WorkingDirectory $unrelated.FullName | Out-Null
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
    $main = @(Get-CimInstance Win32_Process -Filter "Name='Spotify.exe'" | Where-Object {
        $_.ExecutablePath -ieq $exe -and $_.CommandLine -notmatch '--type=|--url='
    })
    if (-not $main.Count) { throw 'Spotify main process exited after hook installation' }
    if (Test-Path (Join-Path $unrelated.FullName 'blockthespot.log')) { throw 'Log incorrectly followed the working directory' }
    Uninstall-BlockTheSpot -SpotifyDirectory $SpotifyDirectory
    if ((Get-FileHash $chrome).Hash -ne $originalHash) { throw 'Uninstall did not restore the exact genuine DLL' }
    @"
PASS: Spotify $version installs/reinstalls safely, starts from an unrelated working directory, intercepts imports and decodes actual URL/ZIP callbacks. Uninstall restores the exact original DLL.
The release-download boundary uses this run's local artifacts; signature/hash/architecture checks and file transactions are real.
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
