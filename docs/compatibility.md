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
| Native Windows tests | Production import walker and dispatch across kernel32/API-set providers, unresolved/resolved delay slots, repeated hooks, malformed inputs, protection restoration, pointer guards and transactional patches | Every real-world ABI or installation |
| Real-client startup smoke | Built DLLs load into current official Spotify; hooks report installation; URL and ZIP callbacks execute and decode strings; client remains running during the short observation | Ad-free playback, a signed-in session, all banner rendering, or long-running stability |

CI requires all three stages before uploading distributable binaries. Failed runs retain available diagnostic reports. The daily watcher reruns verification even if the version has not changed, opens/updates an issue on failure, and does not publish a release on failure. Manual releases use the same gate and the version actually tested, not a stale config comment.

Known default offsets remain `get_url=0x30`, `zip.read_file=0x70`, `zip.get_file_name=0x48`. The version override table is currently empty. A valid pointer within executable libcef memory can still be the *wrong method* after an ABI change; this is not automatic offset discovery and is not a universal crash guarantee.
