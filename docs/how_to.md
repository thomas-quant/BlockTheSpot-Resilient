# BlockTheSpot how to create the signatures for advanced users.

This guide documents the workflow for rebuilding, launching, debugging, and updating stale signature-based patches in this repo.

It is written around the findings from the Spotify `1.2.86.502.g8cd7fb22` desktop build that was inspected on April 18, 2026.

## Scope

This guide covers:

- getting the project to build in Visual Studio
- launching Spotify under the debugger with the correct working directory
- reading the existing logs
- identifying whether the native hooks are still healthy
- dumping the intercepted JS files in a `Debug|x64` build
- rebuilding stale `config.ini` signatures
- maintaining non-restricted telemetry/metrics-related signatures

This guide does not document restricted-feature or upsell-removal patching.

## Current Findings

From the current debug logs, the following pieces are already working:

- `blockthespot.dll` loads and `Loader initialized successfully.` is logged
- the developer byte patch still matches
- `cef_urlrequest_create` hooking is live
- `cef_zip_reader_create` / `read_file` hooking is live
- `xpui-snapshot.js` and `xpui-pip-mini-player.js` are still being intercepted
- the current `LIBCEF` offsets are still valid for this build

The stale signature failures that were observed are:

- `miniplayer_begin`
- `skipsentry`
- `disable_metric`

For safe maintenance work, focus on the telemetry/metrics-related patterns (`skipsentry`, `disable_metric`) and the general porting workflow.

## Files That Matter

- `Hook/cef_zip_reader_hook.cpp`
- `Hook/cef_url_hook.cpp`
- `Hook/developer_mode.cpp`
- `Hook/pattern.cpp`
- `Hook/loader.cpp`
- `Hook/loader.h`
- `Loader/dllmain.cpp`
- `config.ini`

## Prerequisites

Install or verify:

- Visual Studio with Desktop development with C++
- x64/x86 C++ build tools
- MASM support

This repo uses MASM in `Loader/chrome_dll.asm`, so a partial C++ install is not enough.

If Visual Studio reports that `v145` build tools are missing, you have two supported options:

1. Install the `v145` toolset.
2. Retarget both projects to the toolset you do have installed.

If you retarget, make sure you retarget both:

- `Hook/Hook.vcxproj`
- `Loader/Loader.vcxproj`

## Build Configuration

Use:

- `Debug|x64` while debugging
- `Release|x64` only after the signatures are stable

Do not use `Win32` for this workflow.

## Spotify Folder Layout

Before launching Spotify under the debugger, the Spotify folder should contain:

- `Spotify.exe`
- `chrome_elf.dll` from this repo
- `chrome_elf_required.dll` (the original Spotify DLL renamed)
- `blockthespot.dll`
- `blockthespot.pdb`
- `config.ini`

The repo loads files with relative paths such as `./blockthespot.dll`, `./chrome_elf_required.dll`, and `./config.ini`, so the working directory matters.

## Launching Spotify From Visual Studio

### 1. Set the startup project

Set `Hook` as the startup project.

### 2. Configure debugging

Open `Hook` project properties and set:

- `Configuration` = `Debug`
- `Platform` = `x64`
- `Configuration Properties -> Debugging -> Command` = full path to `Spotify.exe`
- `Configuration Properties -> Debugging -> Working Directory` = Spotify install folder
- `Configuration Properties -> Debugging -> Command Arguments` = empty
- `Configuration Properties -> Debugging -> Debugger Type` = `Native Only`

### 3. Launch

Press `F5`.

If Visual Studio says `blockthespot.dll is not a valid Win32 application`, that means it is trying to launch the DLL directly instead of launching `Spotify.exe`. Recheck the `Command` and `Working Directory` settings.

## Logging

Set in `config.ini`:

```ini
[Log]
Level=2
```

This gives the most useful signal while porting.

## How To Read The Current Logs

These messages mean the native side is healthy:

- `init_log_thread: initialized`
- `do_hook_developer: patch applied.`
- `do_hook_cef_url: patch applied.`
- `do_hook_cef_zip_reader: patch applied.`
- `Loader initialized successfully.`

These messages are expected debug noise from the current patch parser:

- `signature_2 empty, stop processing`

Those messages are not, by themselves, failures.

The messages that matter are:

- `FindPattern failed.`

That means the current signature no longer matches the new JS buffer.

## Debug-Only JS Dump Helper

The current `Debug|x64` build contains a helper in `Hook/cef_zip_reader_hook.cpp` that writes:

- `dump_xpui-snapshot.js`
- `dump_xpui-pip-mini-player.js`

to the Spotify working folder once per launch.

This happens before patching, so those files are the cleanest source for signature repair.

Use those dump files instead of trying to search a huge buffer in the Visual Studio Memory window.

## Recommended Porting Order

### Step 1. Confirm native hooks still work

Do not touch `config.ini` yet.

Launch Spotify and make sure the log still shows:

- the loader initialized
- URL blocking hooks firing
- zip-reader file names being logged

If the hook layer is not healthy, stop and fix that first. A stale JS signature is much easier than a broken loader.

### Step 2. Inspect the dump files

Open:

- `dump_xpui-snapshot.js`
- `dump_xpui-pip-mini-player.js`

Use a normal editor or a hex editor. Searching those dumped files is much easier than searching the debugger memory window.

### Step 3. Pick stable anchors

When rebuilding a signature:

- prefer semantic tokens
- prefer readable API names
- prefer translation keys or function names that are likely to survive minifier churn

Avoid anchoring on:

- hashed CSS class strings
- very short minified identifiers such as `Tk`, `Ve`, `n`, `x_`, `as`

For the current telemetry/metrics-related signatures, the strongest anchors seen so far are:

- `buildVersion`
- `BrowserMetrics`
- `https`

### Step 4. Convert the matching text to hex

Remember:

- the dumped JS text is already bytes
- `config.ini` signatures are those same bytes written in hex
- `??` means wildcard byte

Example:

```text
function Tk(e){
```

becomes:

```text
66 75 6E 63 74 69 6F 6E 20 54 6B 28 65 29 7B
```

### Step 5. Wildcard unstable bytes

Replace unstable minified names with `??`.

Good example:

```text
function ??(e){??.BrowserMetrics
```

Bad example:

```text
function Tk(e){$y.BrowserMetrics
```

The second version is too tied to one minifier pass.

### Step 6. Recalculate the offset

`Offset_1` is the number of bytes from the start of the matched signature to the first byte that should be overwritten.

The patching flow is:

1. `FindPattern` locates the start of the match.
2. `Offset_1` moves from that start to the exact patch point.
3. `Value_1` is written there.

Do not blindly reuse an old offset after changing the signature shape.

If the old prefix disappeared, the old offset is probably wrong even if the semantic target is the same.

## Practical Workflow For `skipsentry`

1. Search `dump_xpui-snapshot.js` for `buildVersion`.
2. Confirm the surrounding code still leads into a reporting call with an `https` URL.
3. Build a new signature anchored on that stable tail instead of the old stale function header.
4. Recalculate the offset from the new signature start.
5. Re-run and check whether `FindPattern failed` disappears for `skipsentry`.

Notes:

- The old `skipsentry` prefix `function ??(){if(??()){...` no longer matches the current code shape.
- The stable part that survived is the `buildVersion` plus reporting-call region.

## Practical Workflow For `disable_metric`

1. Search `dump_xpui-snapshot.js` for `BrowserMetrics`.
2. Copy a stable chunk around the metrics call.
3. Convert that chunk to hex.
4. Wildcard minified identifiers.
5. Update `Signature_1`.
6. Recalculate `Offset_1` if the signature start moved.
7. Re-run and confirm the pattern now matches.

## Practical Workflow For `miniplayer_begin`

The general process is the same:

1. Search `dump_xpui-pip-mini-player.js`.
2. Anchor on stable semantic text rather than hashed class names.
3. Rebuild the signature and recalculate the offset.

This guide intentionally stops at the general maintenance pattern and does not document restricted-feature or upsell-removal patching.

## Visual Studio Breakpoint Tips

If a parameter like `patch_name` is hard to inspect in `do_patch_buffer`, move one frame earlier.

Use a breakpoint on the call site in `patch_file`:

```cpp
do_patch_buffer(file_name, patch_name, buffer, bufferSize);
```

At that point:

- `patch_name` is a real local array
- `file_name` is still visible
- `buffer` and `bufferSize` are easy to inspect in `Locals`

Conditional breakpoint examples:

```cpp
file_name != nullptr &&
patch_name != nullptr &&
strcmp(file_name, "xpui-snapshot.js") == 0 &&
strcmp(patch_name, "disable_metric") == 0
```

Remember:

- `file_name == "xpui-snapshot.js"` is wrong for `const char*`
- use `strcmp(...) == 0`

## Troubleshooting

### `v145` toolset cannot be found

Either:

- install `v145`
- or retarget the projects to the installed toolset

### `blockthespot.dll is not a valid Win32 application`

Visual Studio is trying to run the DLL directly.

Fix:

- set `Command` to `Spotify.exe`
- set the working directory to the Spotify folder
- keep the platform on `x64`

### The debugger launches Spotify but hooks do not work

Check:

- `chrome_elf.dll` from this repo is in the Spotify folder
- the original DLL was renamed to `chrome_elf_required.dll`
- `blockthespot.dll` and `config.ini` are in the same folder
- Visual Studio is launching with the Spotify folder as working directory

### The Memory window is too hard to search

Use the dump files instead.

That is the recommended workflow now.

### `signature_2 empty` spam

That is expected with the current `do_patch_buffer` parser and single-signature patch sections.

Focus on whether `FindPattern failed` is still appearing.

## Verification Checklist

Before moving back to `Release|x64`, verify:

- the project builds cleanly
- Spotify launches from Visual Studio
- the native hook layer initializes successfully
- URL blocking still logs as expected
- `dump_xpui-snapshot.js` and `dump_xpui-pip-mini-player.js` are written in `Debug|x64`
- the stale signature you updated no longer logs `FindPattern failed`

Only after that should you:

1. switch to `Release|x64`
2. copy fresh release DLLs to the Spotify folder
3. reduce log level back down if desired

## Summary

The shortest reliable loop is:

1. build `Debug|x64`
2. launch Spotify from Visual Studio with the Spotify folder as working directory
3. inspect logs
4. open the dumped JS files from disk
5. rebuild one stale signature at a time
6. recalculate the offset from the new signature start
7. repeat until `FindPattern failed` disappears for the target section

Do not try to fix multiple stale signatures at once.
