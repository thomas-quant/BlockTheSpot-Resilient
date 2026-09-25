# Compatibility investigation and verification scope

## September 2026 hook failure

Reports: [#1](https://github.com/thomas-quant/BlockTheSpot-Resilient/issues/1) and [#2](https://github.com/thomas-quant/BlockTheSpot-Resilient/issues/2).

Static inspection found the following in `Spotify.dll`:

| Spotify version | Provider of `GetProcAddress` | CEF import style |
|---|---|---|
| 1.2.93.667 (working local installation) | `KERNEL32.dll` | Delayed imports |
| 1.3.1.234 (official current download) | `api-ms-win-core-libraryloader-l1-2-0.dll` | Delayed imports |

The old implementation searched only `KERNEL32.dll`. It silently missed the new provider and its return value was ignored. Both versions' banner signatures passed the old Python verifier, so auto-builds did not establish that either native callback was installed.

The extracted 1.3.1.234 `Spotify.dll` had a **valid Spotify AB Authenticode signature**. SHA-256: `0731eca3ec438395815907c04653c63a917b55bf0ebdd83c96f041424a92b54b`.

The first affected release was not established. MichaelMiksa's release targets 1.2.96.518 and a reporter confirmed it fixed their problem, but that is not a binary bisect. The initial diagnosis was static inspection, not a playback reproduction. The working personal installation was not updated or patched during investigation.

## Fix

MichaelMiksa's [delay-import implementation](https://github.com/MichaelMiksa/BlockTheSpot---continued/commit/7725515) and [loader integration](https://github.com/MichaelMiksa/BlockTheSpot---continued/commit/52c82f3) inspired the direct CEF import interception. Credit is retained in code, README, and the fixing commit's co-author trailer.

This fork adapts the approach rather than merging the entire fork:

- Match `GetProcAddress` by symbol, independent of its Windows API-set provider.
- Patch both ordinary and delay-import slots for both CEF creation functions in `Spotify.exe` and `Spotify.dll`.
- Resolve originals before publishing any CEF stubs. Keep the original `GetProcAddress` from our own import, not another module's potentially hooked slot.
- Bound PE traversal, skip ordinal imports, reject unsupported legacy delay descriptors, check page-protection operations, and report partial results.
- Preserve CEF guards; reject null/truncated objects and non-executable or out-of-module pointers.
- Do not share URL/patch scratch buffers across callbacks. Free returned CEF strings.
- Scan only returned SPA bytes, require unique matches, validate offsets, and apply paired edits only after both have been validated.

## What each check establishes

| Check | Establishes | Does not establish |
|---|---|---|
| Python verifier/tests | Config structure, all configured signature halves, unique matches, bounds and sequential patch ordering on extracted files | Native interception or chunk boundaries of actual reads |
| Native Windows tests | Production import walker and dispatch, malformed inputs, protection restoration, pointer guards, transactional patches, Unicode config paths and signature redirection scope/state handling | Every real-world ABI or installation |
| Loader ABI tests | Production assembly forwarders preserve integer, XMM and stack arguments under concurrent calls; resolver deliberately overwrites all volatile argument registers and uses shadow space | Every future export signature or Chromium export set |
| Installer tests (PowerShell 5.1 and current pwsh) | Real file transactions and hashes with isolated Unicode-path fixtures; failed downloads, corrupt/wrong-architecture assets, reinstallation, locked files, rollback, concurrency and unsafe uninstall refusal | Public network availability or power-loss atomicity; fixture signature/version checks are mocked |
| Real-client startup smoke | Real installer validation against Spotify-signed binaries, installation/reinstallation, startup from an unrelated directory, decoded URL/ZIP callbacks, a live main process, and exact original-DLL restoration on uninstall | Public release download (substituted with this run's built artifacts), ad-free playback, a signed-in session, all banner rendering, or long-running stability |

CI requires all stages before uploading distributable binaries. Failed runs retain available diagnostic reports. The daily watcher reruns verification even if the version has not changed, opens/updates an issue on failure, and does not publish a release on failure. Manual releases use the same gate and the version actually tested, not a stale config comment.

## Additional pre-merge hardening

The loader originally relied on the current working directory, used a mutable `unordered_map` concurrently, and called its resolver without x64 shadow space or preserving XMM arguments. It now uses module-relative Unicode paths, one-time loading of the original DLL, thread-safe export resolution and ABI-correct forwarding with unwind metadata. A missing mandatory original export still cannot be generically emulated; it fails explicitly rather than jumping to null.

Config/log/library paths are module-relative, and INI reads use Unicode Win32 APIs. The signature hook validates the union selector, redirects only the actual proxy path, uses per-call file-info copies, and propagates the trust-state handle needed by VERIFY/CLOSE. The payload is pinned until process exit: live import pointers and logger threads must never point into an unloaded DLL. The old deletion of `dpapi.dll` from the process working directory was removed.

Installation pins one stable GitHub release and requires SHA-256 digests, asset sizes and x64 DLL headers before stopping the app. A signed original DLL must match libcef's embedded Chromium version. A same-directory lock serializes installer/uninstaller runs; replacements use same-volume atomic file operations. Caught replacement failures restore snapshots, and failed recovery leaves those snapshots and a journal for manual repair. Uninstall verifies/restores a genuine DLL before removing the payload and never overwrites a newer genuine DLL with a stale backup.

**Limits:** a set of individually atomic file replacements is not an atomic multi-file transaction across power loss or forced termination. Retained recovery snapshots are not automatically replayed. A digest from the same GitHub release detects corruption and mixed releases, not a compromised release account. Refusing an unknown Chromium version format is intentional rather than guessing backup compatibility.

Known default offsets remain `get_url=0x30`, `zip.read_file=0x70`, `zip.get_file_name=0x48`. The version override table is currently empty. A valid pointer within executable libcef memory can still be the *wrong method* after an ABI change; this is not automatic offset discovery and is not a universal crash guarantee.
