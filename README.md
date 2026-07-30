# BongoCat

`BongoCat` is a small native desktop overlay for Live2D models. It keeps a
borderless, transparent window on the desktop and maps keyboard, mouse, and
gamepad input to the model.

This README is about the source tree and its build. It is not a user manual.
The Windows build with the Live2D Cubism SDK is the reference build; the public
CI jobs build and test the same program with a diagnostic renderer because the
SDK is licensed and is not checked into this repository.

## Current State

The native application, bundled model packages, preferences, import path, and
platform integrations are in this tree. The evidence ledger in
[`docs/feature-audit.md`](docs/feature-audit.md) records what has been exercised
on a real Windows Cubism build and what still needs a manual check on a
particular operating system or device.

Without Cubism, CMake selects a diagnostic backend. The application still
starts, opens its preferences, processes input, and runs the native tests, but
it cannot draw or animate a real Live2D model. Configure with
`-DBONGO_CAT_REQUIRE_CUBISM=ON` when a missing SDK should be a configuration error
instead of a diagnostic build.

## Features

- Standard, keyboard, and gamepad model modes, with three bundled model
  packages.
- Cubism `.model3.json` loading, textures, motions, expressions, physics, pose,
  eye blink, and breath updates.
- Import and removal of custom model packages. A package is checked for a valid
  manifest and safe file references before it is installed under the user data
  directory.
- Automatic discovery of Bongo-Cat-Mver packages placed beside the executable,
  either as `config.json` + `img/` directly or as an intact child directory.
  Model containers with nested full packages and image-only variants (for
  example `A-*` plus `Z-*` directories) are also discovered. The source stays
  read-only and BongoCat caches only generated adapter files.
- Keyboard and mouse input, pointer tracking, gamepad buttons and axes,
  mirroring, automatic key release, and configurable shortcuts.
- Transparent borderless window, click-through, always-on-top, hover hiding,
  monitor clamping, scaling, opacity, tray integration, and Shift + right-drag
  resizing.
- Preferences, light/dark themes, and five shipped locales: English,
  Simplified Chinese, Traditional Chinese, Brazilian Portuguese, and
  Vietnamese.

## How the Runtime Is Split

The program is intentionally split at the points where the implementation has
different ownership:

```text
SDL events and native hooks
            |
            v
     input state (C11 atomics) ---> main-thread application
                                             |
                              model parameters and UI state
                                             |
                              Cubism update -> OpenGL draw
```

The platform hooks do not touch a model. They publish timestamped events to a
bounded queue; pointer coordinates are coalesced separately so mouse movement
does not consume the queue needed for key and button transitions. The main loop
drains the queue, applies releases and parameters, updates the model, and swaps
the SDL OpenGL window.

Frames are submitted when something changed. An idle Cubism model is allowed to
accumulate a short time slice before it updates, which keeps an idle transparent
window quiet without making motions depend on a fixed frame rate.

The C runtime calls a small interface in `src/live2d`. The Cubism implementation
is C++17 because that is the SDK's API; the rest of the application remains C11.
This keeps SDK types out of the public headers and makes the diagnostic backend
possible.

## Source Tree

```text
include/bongo_cat/       public C interfaces and data structures
src/core/             config, paths, input state, catalogs, hashes
src/runtime/          application lifecycle, model import, and UI flow
src/render/           OpenGL helpers and overlays
src/live2d/           Cubism bridge and diagnostic backend
src/platform/         Win32, Cocoa, X11, input, and tray adapters
src/ui/               Nuklear backend, preferences, themes, localization
resources/assets/     bundled models, textures, locales, and tray assets
tests/                native tests and fixtures
cmake/                dependency, optimization, audit, and source-policy modules
docs/                 audit evidence and parity notes
```

## Dependencies

The build uses SDL3 for the window and event loop, desktop OpenGL for drawing,
yyjson for JSON, stb for image loading, miniaudio for motion audio, and
Nuklear for the preferences UI. With `BONGO_CAT_FETCH_DEPS=ON` (the default), CMake
fetches pinned revisions of those open-source dependencies and verifies their
SHA-256 values. Set it to `OFF` to use installed packages and headers instead.

The Live2D Cubism SDK is a separate download. Put Cubism SDK for Native 5 r.5
at `vendor/CubismSdkForNative` after accepting Live2D's license. The expected
tree contains `Core`, `Framework`, and the SDK's OpenGL sample dependencies.
On Windows, run the SDK helper once:

```powershell
cd vendor\CubismSdkForNative\Samples\OpenGL\thirdParty\scripts
.\setup_glew_glfw.bat
```

The SDK is ignored by Git and is never fetched by this project.

## Building

### Windows with Cubism

Use Visual Studio 2022 or the matching Build Tools (v143, Windows 10/11 SDK):

```powershell
cmake -S . -B build-cubism -G "Visual Studio 17 2022" -A x64 `
  -DBONGO_CAT_REQUIRE_CUBISM=ON `
  -DBONGO_CAT_WARNINGS_AS_ERRORS=ON
cmake --build build-cubism --config Release --parallel 2
ctest --test-dir build-cubism -C Release --output-on-failure
```

The executable is `build-cubism/Release/BongoCat.exe`.

Build the versioned release archive and SHA-256 file with:

```powershell
cmake --build build-cubism --config Release --target package
```

Release archives use the conventional product-version-platform-architecture
format, for example `BongoCat-0.1.0-windows-x64.zip`. The executable inside
keeps the stable `BongoCat.exe` name so upgrades do not break shortcuts.

### Diagnostic or Unix build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The native Linux branch requires the usual OpenGL and X11/XInput2/Xfixes
development packages. macOS uses its Cocoa and ApplicationServices frameworks.

To use system dependencies rather than CMake's pinned downloads:

```powershell
cmake -S . -B build -G Ninja -DBONGO_CAT_FETCH_DEPS=OFF
```

When fetching is disabled, CMake needs SDL3-static and yyjson package
configurations plus `stb_image.h`, `stb_image_write.h`, `miniaudio.h`, and
`nuklear.h`. Their include roots can be set with
`BONGO_CAT_STB_INCLUDE_DIR`, `BONGO_CAT_MINIAUDIO_INCLUDE_DIR`, and
`BONGO_CAT_NUKLEAR_INCLUDE_DIR`.

### Install tree

```powershell
cmake --install build --config Release --component Runtime --prefix out
```

The install tree is portable. Windows embeds the resource archive; Unix builds
place `assets/` beside the executable or application bundle. Settings and
imported models are kept in the per-user data directory.

## Runtime Data and Test Switches

The default data directory comes from SDL's preference path and contains
`preferences.json`, `session.json`, `custom-models/`, and the generated `portable-mver/` adapter
cache. A Bongo-Cat-Mver package can be used without importing by placing its
`config.json` and `img/` beside `BongoCat`, or by placing the intact package
directory there. A collection such as `露西亚-誓焰版` may be placed as one
folder; its full package and nested image patches are discovered together.
Tests and portable launches may override both paths:

```text
BongoCat --data-root=C:\path\to\data --preferences=C:\path\to\preferences.json --session=C:\path\to\session.json
```

Arguments beginning with `--ci-` are test instrumentation. They select a model,
preference page, language, theme, frame audit, or input audit; they are not a
stable end-user command-line interface.

## Tests and Audits

The CTest suite covers configuration migration, model discovery, input ordering
and recovery, shortcuts, localization, UI helpers, application state, and
SHA-256 resource validation.

```powershell
ctest --test-dir build --output-on-failure
cmake --build build --target check-lines
```

`check-lines` enforces a maximum of 300 physical lines for native C, C++,
Objective-C, header, CMake module, and native test files. The rule is a review
aid, not a claim that shorter files are automatically better.

The Windows audit scripts exercise window styles, preferences, input hooks,
model import, frame output, and hidden-window behavior. For example:

```powershell
& .\cmake\VisualAudit.ps1 -Exe .\build-cubism\Release\BongoCat.exe `
  -OutputDir .\build-cubism\visual-audit -SkipMain
& .\cmake\SoakAudit.ps1 -Exe .\build-cubism\Release\BongoCat.exe `
  -OutputDir .\build-cubism\soak-audit -Mode hidden -DurationSeconds 60
```

The scripts keep their logs and images in the selected output directory. A
diagnostic frame is useful for troubleshooting, but it is not evidence that a
Live2D feature works.

## Continuous Integration

GitHub Actions runs Cppcheck, the source-size check, a warning-clean Linux build,
CTest, and a Windows/Linux/macOS build matrix on pushes, pull requests, tags,
and manual runs. CI artifacts use the diagnostic backend; a licensed Cubism SDK
is not provisioned on the runners.

## Third-Party and Distribution Notes

Third-party components retain their upstream licenses. Cubism SDK files must be
obtained from Live2D and are not redistributed here. Before distributing a
binary, review the licenses for SDL3, yyjson, stb, miniaudio, Nuklear, OpenGL,
and the selected platform SDK together with the project's own distribution
terms.
