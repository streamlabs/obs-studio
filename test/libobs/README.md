# Native libobs tests

These tests use Catch2 v3.11.0, matching OSN, and run against this checkout's
libobs through CTest. They require no video reset, GPU rendering, plugins,
credentials, network access, or user configuration. Configuring the build still
requires the normal OBS dependencies and downloads Catch2 unless already cached.

The initial cases follow the public API documentation:

- [Core lifecycle](../../docs/sphinx/reference-core.rst): start OBS, verify
  `obs_initialized()`, shut down, and verify that it is no longer initialized.
- [Data settings](../../docs/sphinx/reference-settings.rst): create a data object,
  write and read scalar values, and release it before shutting down OBS.
- [Scenes](../../docs/sphinx/reference-scenes.rst): create a scene, access its
  borrowed source, and release the scene before shutting down OBS.

Utility cases cover dynamic-array append/free, bitstream reads and end-of-input,
array serialization and position tracking, and path-extension parsing with
Windows and Unix separators. These helpers do not require OBS startup.

Windows-only file cases check regular file creation, concurrent writers, and
rejection of directory junctions and symbolic links without modifying their
targets. They create temporary files and remove them after each case, including
on assertion failures. The symbolic-link case is reported as skipped if Windows
denies the required privilege; enable Developer Mode or run with the
create-symbolic-link privilege to include it. Other setup failures fail the test.

## Build and run

From the repository root on Windows:

```powershell
cmake --preset windows-ci-x64 -DBUILD_TESTING=ON
cmake --build build_x64 --config RelWithDebInfo --target libobs_unit_tests
ctest --test-dir build_x64 -C RelWithDebInfo -R "^libobs_unit_tests::" --output-on-failure --no-tests=error
```

On macOS, configure with `--preset macos-ci -DBUILD_TESTING=ON`, then use
`build_macos` instead of `build_x64` in the build and test commands.

To run only the startup/shutdown case, use
`-R "^libobs_unit_tests::OBS initializes and shuts down$"`.
CTest supplies the build's runtime library paths. Each discovered test runs in
its own process and has a 30-second timeout. The scope guard also shuts OBS down
if an assertion aborts a case; owned objects must be declared after that guard
so they are released first.

Tests are enabled in the Windows x64 and macOS CI presets. Other configurations
can opt in with `BUILD_TESTING=ON`. Tests and Catch2 are not installed into the
product package.

Add small cases describing documented public behavior here. Keep tests requiring
video rendering, hardware encoders, or plugins in separate suites with explicit
runtime requirements.
