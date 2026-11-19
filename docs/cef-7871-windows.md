# CEF 7871 on Windows x64

The OBS 33 merge selects `cef_binary_7871_windows_x64_v1.zip` for Windows x64. The archive is built from `obsproject/cef:7871_obsproject` at `abc1ece6a66997894c4c953ed24937603fbd74b4` with Chromium `150.0.7871.287`, Release binaries, matching PDBs, and sandbox bootstrap launchers. The runtime ZIP SHA-256 is `a7d055b2ea24cefb4963d5751d181343c1941a7095091eead8c5637405eff153`. A separate symbols archive is available at the same S3 base URL as `cef_binary_7871_windows_x64_v1_symbols.7z`.

CEF M138 and newer cannot link `cef_sandbox.lib` into an independently built Windows host. The OBS development package exposes `OBS::cef-bootstrap`, `OBS::cef-bootstrapc`, `OBS::cef-bootstrap-win`, and `OBS_CEF_CHROME_ELF` instead. The host must be a DLL exporting `RunConsoleMain` or `RunWinMain`; the matching bootstrap executable must run under the host's executable name beside the DLL and `chrome_elf.dll`. Forward the opaque sandbox pointer to `CefExecuteProcess` and `CefInitialize`. The bootstrap owns that pointer.

The `obs-browser` plugin reads its sandbox ABI from the executable first, then the bootstrap-loaded DLL with the executable's stem. Streamlabs `obs-studio-node` supplies that DLL as `obs64.dll` and installs `bootstrapc.exe` as `obs64.exe`. The bootstrap launchers are included in the OBS browser runtime and development package.

CEF bootstrap checks Authenticode before loading the host: the final launcher, `chrome_elf.dll`, and host DLL must all be unsigned or all have valid signatures with the same primary certificate. Sign and verify those three final installed files together in the release pipeline. A signed host DLL with unsigned CEF binaries will fail at startup.

The pinned build worker blocks CEF child-process IPC. Archive integrity and all nine binary/PDB identities were verified; sandboxed startup, renderer/GPU child processes, elevated Windows Graphics Capture, and NVIDIA GPU preference require validation in a normal packaged application before this change is ready to merge.
