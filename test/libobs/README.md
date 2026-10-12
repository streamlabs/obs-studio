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

## Low-level tests

The low-level tests cover containers, calldata, integer arithmetic, CPU math,
CRC32, audio and frame-rate helpers, packed colors, sRGB, half floats, Unicode,
lexers, and callback declaration parsing. They do not start OBS or require
plugins, graphics devices, or capture devices. Owned buffers are released by
local scope guards. Deque and string tests include deterministic mixed-operation
sequences checked against standard-library containers; failures report the
operation and step so the fixed-seed sequence can be reproduced.

After building, run the container, calldata, integer, and transform tests with:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure --no-tests=error -R "libobs_unit_tests::(Deque|Dstr|Calldata|Integer|Math)"
```

Run the checksum, media-helper, color, Unicode, and parsing tests with:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure --no-tests=error -R "libobs_unit_tests::(CRC32|Audio helpers|Frame rate|Color|Half|Unicode|Lexer|Declaration)"
```

Unicode fixtures account for UTF-16 versus UTF-32 wide characters and the
different malformed-input policies of the Windows and portable converters.
Buffer-boundary tests place guard values outside the advertised destination
capacity. CRC and color conversions include fixed reference values, and lexer
tests assert token content and order as well as success or failure.

Container assertions remain enabled in the test executable even in release
configurations. Integer tests exercise the portable fallback on every platform,
including comparisons against native wide arithmetic where available. Windows
Unicode tests also place short input slices immediately before an inaccessible
guard page to detect reads beyond the supplied length.

Callback fixtures also check signal delivery and procedure input/output through
the real handlers without starting OBS. The lifecycle smoke tests exercise the
built-in source signal declarations and audio sync callback adjustments.
The VLC metadata declaration is shared with the plugin, so the registration test
uses its actual signature without loading VLC. Source creation must not emit
declaration diagnostics, including warnings from the parser.
