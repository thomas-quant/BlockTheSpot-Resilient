<div align="center">

# BlockTheSpot-Resilient

**Spotify ad blocking for Windows, with safer hooks and compatibility checks.**

[![Latest release](https://img.shields.io/github/v/release/thomas-quant/BlockTheSpot-Resilient?label=latest%20build&color=1DB954)](https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/latest)
[![Build and Verify](https://github.com/thomas-quant/BlockTheSpot-Resilient/actions/workflows/ci.yml/badge.svg)](https://github.com/thomas-quant/BlockTheSpot-Resilient/actions/workflows/ci.yml)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue)

</div>

A maintained fork of [BlockTheSpot](https://github.com/mrpond/BlockTheSpot), built on [Nuzair46's continuation](https://github.com/Nuzair46/BlockTheSpot). It patches your existing standalone Spotify client rather than downloading a replacement from a third-party mirror.

- **More than one interception path.** Hooks CEF's ordinary and delayed imports by name, plus dynamic `GetProcAddress` lookups, including Windows API-set imports used by newer Spotify versions.
- **Defensive patching.** Checks CEF object sizes, function-pointer ranges and memory protections. JavaScript patches need unique matches and bounded writes; paired edits are validated before either is applied.
- **Checks that exercise the hooks.** Branch CI and the daily watcher run native regression tests, validate JS patches, then launch the official Spotify client on a disposable Windows runner and require actual URL and ZIP callbacks.
- **Honest failure reporting.** Failed verification blocks publication and the watcher opens an issue. Startup diagnostics distinguish prepared handlers, installed imports and observed callbacks.

**Limits:** CEF offsets are known defaults with a version-override mechanism, not automatic ABI discovery. Guards cannot prove that a valid function pointer still identifies the expected method. The logged-out startup smoke test does **not** establish ad-free playback or signed-in UI behavior. No fork can guarantee compatibility with every future Spotify update. See [compatibility and testing](docs/compatibility.md).

## Install

Requires the **standalone Windows x64 client**, not the Microsoft Store version. In PowerShell:

```powershell
iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/install.ps1 | iex
```

[Read the installer first](install.ps1). It stops Spotify, backs up the genuine `chrome_elf.dll`, downloads the latest release files and relaunches the app. A Spotify update can replace the loader; re-run the installer if necessary.

### Manual install

1. Close Spotify and open `%AppData%\Spotify`.
2. Rename the **genuine** `chrome_elf.dll` to `chrome_elf_required.dll`. Do not overwrite a genuine backup with an already-installed BlockTheSpot loader.
3. Download `chrome_elf.dll`, `blockthespot.dll` and `config.ini` from the [latest release](https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/latest) into that folder.
4. Launch Spotify.

### Uninstall

```powershell
iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/uninstall.ps1 | iex
```

## How it works

- **`chrome_elf.dll` (loader)** forwards Chromium exports to the original signed DLL, renamed `chrome_elf_required.dll`, and loads the payload.
- **`blockthespot.dll` (payload)** intercepts CEF URL requests and SPA reads. It redirects Spotify's signature check to the original DLL rather than modifying Spotify's signed binaries on disk.
- **URL rules** block configured ad request paths. The default config also blocks `/desktop-update/`; remove that rule if you want Spotify's update requests to pass through.
- **SPA patches** hide the leaderboard banner using a length-preserving JavaScript edit.

The URL and UI rules are separate, but **both depend on working native interception**. A matching JS signature alone is not proof that ad blocking works.

## Troubleshooting

Information-level logging is enabled by default in `config.ini` (`[Log] Level=1`). Look in `%AppData%\Spotify\blockthespot.log` after launching:

- `CEF hook installation: OK` — the required interception paths were installed, not proof of playback behavior.
- `CEF URL handler active (URL decoded)` and `CEF ZIP read callback observed (filename decoded)` — actual callbacks executed successfully.
- `FAILED/PARTIAL` or `SPA patch skipped` — compatibility needs investigation; do not treat a successful launch as a successful ad block.

The log is reset on startup. `Level=0` disables it. `Level=2` includes request URLs: **do not post debug logs publicly without redacting tokens and personal information**. When reporting a problem, include the Spotify version, the release/commit installed, symptoms, and the information-level log.

## Development

Cheap cross-platform checks:

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
python tools/verify_patches.py config.ini <extracted-xpui-directory>
```

Windows builds/tests use `tools/build-and-test.ps1` in an x64 MSVC developer shell. GitHub Actions runs this automatically on branches and uploads DLLs plus verification reports. The installer and smoke-test scripts under `tools/` are restricted to disposable GitHub-hosted runners and must not be used against a personal installation.

## Credits

- [**mrpond/BlockTheSpot**](https://github.com/mrpond/BlockTheSpot): original project.
- [**Nuzair46/BlockTheSpot**](https://github.com/Nuzair46/BlockTheSpot): continuation and injection core.
- [**MichaelMiksa**](https://github.com/MichaelMiksa/BlockTheSpot---continued): identified the hook failure and contributed the by-name CEF delay-import approach adapted here, with co-author credit in the fixing commit. See [issue #1](https://github.com/thomas-quant/BlockTheSpot-Resilient/issues/1) and his [original implementation](https://github.com/MichaelMiksa/BlockTheSpot---continued/commit/7725515).

## Disclaimer

For educational use. Modifying the Spotify client may violate Spotify's Terms of Service. Consider [Spotify Premium](https://www.spotify.com/premium/). Use at your own risk.
