# Run from the repository root in an x64 MSVC developer shell.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force reports, out/tests | Out-Null
python -m unittest discover -s tests -p 'test_*.py' -v 2>&1 | Tee-Object reports/python-tests.log
if ($LASTEXITCODE -ne 0) { throw 'Python regression tests failed' }
# Exercise the documented Windows PowerShell 5.1 runtime as well as pwsh.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/installer_tests.ps1 2>&1 | Tee-Object reports/installer-ps5.log
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell installer tests failed' }
pwsh -NoProfile -NonInteractive -File tests/installer_tests.ps1 2>&1 | Tee-Object reports/installer-pwsh.log
if ($LASTEXITCODE -ne 0) { throw 'PowerShell installer tests failed' }
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /IHook tests/native_hooks.cpp Hook/IAT_hook.cpp Hook/libcef_hook.cpp Hook/pattern.cpp Hook/WinTrust_hook.cpp /Foout/tests/ /Feout/tests/native_hooks.exe 2>&1 | Tee-Object reports/native-build.log
if ($LASTEXITCODE -ne 0) { throw 'Native regression test compilation failed' }
$unicode = New-Item -ItemType Directory -Force -Path (Join-Path 'out/tests' ('runtime-' + [char]0x65e5 + [char]0x672c))
Copy-Item out/tests/native_hooks.exe $unicode.FullName -Force
& (Join-Path $unicode.FullName 'native_hooks.exe') 2>&1 | Tee-Object reports/native-tests.log
if ($LASTEXITCODE -ne 0) { throw 'Native regression tests failed' }
ml64 /nologo /c /Foout/tests/chrome_dll.obj Loader/chrome_dll.asm
if ($LASTEXITCODE -ne 0) { throw 'Forwarder assembly failed' }
ml64 /nologo /c /Foout/tests/loader_resolver.obj tests/loader_resolver.asm
if ($LASTEXITCODE -ne 0) { throw 'Test resolver assembly failed' }
cl /nologo /std:c++20 /EHsc /W4 tests/loader_abi.cpp out/tests/chrome_dll.obj out/tests/loader_resolver.obj /Foout/tests/ /Feout/tests/loader_abi.exe
if ($LASTEXITCODE -ne 0) { throw 'Loader ABI test compilation failed' }
& ./out/tests/loader_abi.exe 2>&1 | Tee-Object reports/loader-abi.log
if ($LASTEXITCODE -ne 0) { throw 'Loader ABI regression tests failed' }
msbuild Hook/Hook.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$PWD\" 2>&1 | Tee-Object reports/hook-build.log
if ($LASTEXITCODE -ne 0) { throw 'Hook build failed' }
msbuild Loader/Loader.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$PWD\" 2>&1 | Tee-Object reports/loader-build.log
if ($LASTEXITCODE -ne 0) { throw 'Loader build failed' }
New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item out/x64/Release/blockthespot.dll, out/x64/Debug/chrome_elf.dll, config.ini dist/ -Force
