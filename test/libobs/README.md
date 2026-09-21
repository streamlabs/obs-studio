# Native libobs tests

These tests use Catch2 v3.11.0, matching OSN, and run against this checkout's
libobs through CTest. Configuring the build requires the normal OBS dependencies
and downloads Catch2 unless already cached.

## Build and run

From the repository root on Windows:

```powershell
cmake --preset windows-ci-x64 -DBUILD_TESTING=ON
cmake --build build_x64 --config RelWithDebInfo --target tests
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure --no-tests=error
```

On macOS, configure with `--preset macos-ci -DBUILD_TESTING=ON`, then use
`build_macos` instead of `build_x64` in the build and test commands.

The `tests` target builds all native test executables. CTest runs all registered
tests; rebuild after changing test code.

CTest supplies the build's runtime library paths. Each discovered test runs in
its own process and has a 30-second timeout. The scope guard also shuts OBS down
if an assertion aborts a case; owned objects must be declared after that guard
so they are released first.

Tests are enabled in the Windows x64 and macOS CI presets. Other configurations
can opt in with `BUILD_TESTING=ON`. Tests and Catch2 are not installed into the
product package.

Register new test executables with CTest and add them to the `tests` build target
using `add_dependencies(tests <test-target>)` so these commands continue to build
and run the complete suite.
