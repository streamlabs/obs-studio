# CEF 7871 on Windows x64

OBS 33 pins Windows x64 to CEF 7871 revision 5. The package uses OBS-based CEF commit `ed595aef8b4bf219b5e5fb1c9f600e756bef3019` and Chromium `150.0.7871.289`. The x64 Release build used Visual Studio Community 2026 18.12.1, MSVC 14.52.37025 and Windows SDK 10.0.26100.0. It enables H.264/AAC and crash symbols (`symbol_level=1`) and disables backup reference pointer instance tracing to avoid a browser startup deadlock. [OBS dependency pin](../CMakePresets.json) · [CEF v5 build record](https://github.com/streamlabs/cef/blob/fef901d72702a0caa4c81e74ea317f94e7b927cd/tools/streamlabs/7871/BUILD-PROVENANCE-v5.md)

| Download | SHA-256 |
| --- | --- |
| [Windows x64 runtime and SDK](https://streamlabs-cef-dist.s3.us-west-2.amazonaws.com/cef_binary_7871_windows_x64_v5.zip) | `84fca3d4371266e20e5f3e7d1b3694f38247d25ba6adc2f71c508649e761cf76` |
| [Windows x64 runtime symbols](https://streamlabs-cef-dist.s3.us-west-2.amazonaws.com/cef_binary_7871_windows_x64_v5_symbols.7z) | `b5999a3f1067f708e65895bea6aa3713623be483dbdeea8f89f2660234a11d70` |

The ZIP includes the MSVC `/MT`, `/Zi` wrapper and its compiler PDB together in `build/libcef_dll_wrapper/Release/`. The symbols archive contains nine runtime PDBs. The packager verified each PDB against its binary and linked a C++ consumer against the wrapper. Both hosted archives matched the hashes above.

## Windows launcher and signing

CEF creates the Windows sandbox in its launcher. OSN installs the GUI launcher `bootstrap.exe` as `obs64.exe`, beside `chrome_elf.dll` and a small dispatch DLL, `obs64.dll`. The launcher calls `RunWinMain` in that DLL with sandbox information it owns. On normal startup the DLL loads `osncore.dll`, and the browser plugin passes the sandbox information to `CefInitialize`. For a CEF child process it loads `core/obs-browser/obs-browser-child.dll` and passes the information to `CefExecuteProcess`. This keeps the full OSN runtime out of sandboxed renderers and lets Desktop launch OSN without a console window. [OBS SDK targets](../libobs/cmake/libobsConfig.cmake.in) · [OSN installation](https://github.com/streamlabs/obs-studio-node/blob/ab261d27acd841d28974ce7f4da6e0b1ec32682e/obs-studio-server/CMakeLists.txt) · [OSN process dispatch](https://github.com/streamlabs/obs-studio-node/blob/ab261d27acd841d28974ce7f4da6e0b1ec32682e/obs-studio-server/source/cef-bootstrap.cpp)

For a signed release, sign the final installed `obs64.exe`, `obs64.dll` and `chrome_elf.dll` with the same primary certificate and verify all three signatures. CEF also accepts an all-unsigned set for local development. A mixed set or a certificate mismatch prevents startup. [CEF bootstrap signature checks](https://github.com/streamlabs/cef/blob/fef901d72702a0caa4c81e74ea317f94e7b927cd/libcef_dll/bootstrap/bootstrap_win.cc)

## Validation

Windows tests with v5 exercised browser-source startup, refresh, H.264/AAC playback and recording through stop. Both observed renderers had Win32k system calls blocked. A local Desktop run with OSN `0.28.0test3` rendered a browser frame with no visible helper console.

On a release-signed Desktop installation, verify browser-source startup and hardware acceleration on the selected GPU, including NVIDIA GPU preference. Also run Desktop as administrator and check that the browser source renders while Windows Graphics Capture is active.
