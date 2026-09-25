# Run from the repository root in an x64 MSVC developer shell.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force reports | Out-Null
Start-Transcript -Path reports/build-tests.log -Force | Out-Null
try {
python -m unittest discover -s tests -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python regression tests failed' }
New-Item -ItemType Directory -Force out/tests | Out-Null
cl /nologo /std:c++20 /EHsc /W4 /IHook tests/native_hooks.cpp Hook/IAT_hook.cpp Hook/libcef_hook.cpp Hook/pattern.cpp /Foout/tests/ /Feout/tests/native_hooks.exe
if ($LASTEXITCODE -ne 0) { throw 'Native regression test compilation failed' }
& ./out/tests/native_hooks.exe
if ($LASTEXITCODE -ne 0) { throw 'Native regression tests failed' }
msbuild Hook/Hook.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$PWD\"
if ($LASTEXITCODE -ne 0) { throw 'Hook build failed' }
msbuild Loader/Loader.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$PWD\"
if ($LASTEXITCODE -ne 0) { throw 'Loader build failed' }
New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item out/x64/Release/blockthespot.dll, out/x64/Debug/chrome_elf.dll, config.ini dist/ -Force
} finally {
    Stop-Transcript | Out-Null
}
