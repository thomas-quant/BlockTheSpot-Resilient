<div align="center">

# BlockTheSpot-Resilient

**Block Spotify ads on Windows — without breaking every time Spotify updates.**

[![Latest release](https://img.shields.io/github/v/release/thomas-quant/BlockTheSpot-Resilient?label=latest%20build&color=1DB954)](https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/latest)
[![Stars](https://img.shields.io/github/stars/thomas-quant/BlockTheSpot-Resilient?color=1DB954)](https://github.com/thomas-quant/BlockTheSpot-Resilient/stargazers)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue)

</div>

---

A maintained, **update-resilient** take on [BlockTheSpot](https://github.com/mrpond/BlockTheSpot) — the classic Spotify ad blocker whose original repo is now archived and which breaks on nearly every Spotify update. This fork fixes the two things that made the original painful:

- 🛡️ **It doesn't crash when Spotify updates.** The original hard-codes internal offsets into `config.ini`; when Spotify shifts them, Spotify won't even launch (`0xC0000005`). This version **detects them at runtime** and **guards every access** — a mismatch degrades to "ads not blocked" instead of a dead app.
- 🤖 **It watches Spotify for you.** A CI job checks daily for new Spotify releases, verifies the patches still match, publishes a fresh build, and **opens an issue if anything went stale** — so you're never silently broken.

## Why this vs. the alternatives

| | **This (Resilient)** | BlockTheSpot (original) | SpotX |
|---|:---:|:---:|:---:|
| Survives Spotify updates without crashing | ✅ runtime offset detect + guard | ❌ crashes, needs manual offset edit | ⚠️ re-run each update |
| Auto-builds + alerts when Spotify updates | ✅ CI watcher | ❌ | ❌ |
| Blocks audio + banner ads | ✅ | ✅ *(signatures rot)* | ✅ |
| Patches your **official** Spotify in place | ✅ | ✅ | ⚠️ downloads Spotify from a 3rd-party mirror |
| Keeps Spotify's code signatures intact | ✅ redirects the check | ✅ | ❌ strips signatures |
| Auditable install | ✅ ~40-line PowerShell | ⚠️ compiled installer | ⚠️ remote `iwr\|iex` script |

## Install

Open **PowerShell** and run:

```powershell
iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/install.ps1 | iex
```

It's a short, readable script — [read it first](install.ps1) if you like (you should, for anything that patches an app). It stops Spotify, backs up the original `chrome_elf.dll`, drops in the latest release files, and relaunches.

> **Uninstall** anytime:
> ```powershell
> iwr -useb https://raw.githubusercontent.com/thomas-quant/BlockTheSpot-Resilient/master/uninstall.ps1 | iex
> ```

<details>
<summary><b>Manual install</b> (if you'd rather not run a script)</summary>

1. Go to `%AppData%\Spotify`.
2. Rename `chrome_elf.dll` → `chrome_elf_required.dll`.
3. Download `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` from the [latest release](https://github.com/thomas-quant/BlockTheSpot-Resilient/releases/latest) into that folder.
4. Launch Spotify.
</details>

## How it works

Two DLLs, a decoy-and-payload design:

- **`chrome_elf.dll` (decoy)** replaces Spotify's real one. It re-exports every function Spotify imports and forwards each to the genuine DLL (renamed `chrome_elf_required.dll`), so nothing breaks — then loads the payload.
- **`blockthespot.dll` (payload)** hooks Chromium's networking to block ad requests, patches the ad banner out of the UI, and — cleanly — **redirects Spotify's own signature check** at the original signed DLL rather than stripping signatures.

Ad blocking runs in two independent layers so a Spotify update can't take out both at once:

1. **URL blocklist** — drops ad *fetches* (`/ads/`, `/ad-logic/`, hpto, …). Needs no per-version tuning.
2. **UI patch** — removes the leaderboard ad banner above the playback bar. Anchored on stable tokens with wildcards; if it ever goes stale it no-ops (banner returns, **no crash**) and the [watcher](.github/workflows/spotify-watch.yml) opens an issue.

## Credits

Built on the excellent work of [**mrpond/BlockTheSpot**](https://github.com/mrpond/BlockTheSpot) (original, archived) and [**Nuzair46/BlockTheSpot**](https://github.com/Nuzair46/BlockTheSpot) (continuation). The injection core is theirs; this fork adds update-resilience, the crash guard, a current ad patch, and the auto-build watcher.

## Disclaimer

For educational use. Modifying the Spotify client may violate Spotify's Terms of Service. Consider [Spotify Premium](https://www.spotify.com/premium/) — it's the only way to support artists. Use at your own risk.
