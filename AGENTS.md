# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project Overview

BongoCat is a cross-platform desktop pet (Windows / macOS / Linux) written in
C11 with a C++17 Live2D bridge. It renders a Live2D (or built-in) model in a
transparent desktop window using SDL3 and OpenGL, and animates it from
keyboard / mouse / gamepad input. Public headers live in `include/bongo_cat/`;
the runtime is built as static libraries (`bongo_cat_core`, `bongo_cat_runtime`)
linked into one executable (`src/main.c` -> `bongo_cat_app_run`).

Third-party deps (SDL3, yyjson, stb, miniaudio, Nuklear) are fetched at
configure time via `BONGO_CAT_FETCH_DEPS=ON` (default), so the first configure
needs network access. The Live2D Cubism SDK is optional and never fetched: see
"Cubism SDK" below.

## Build Commands

Run from the project root (the directory containing `CMakeLists.txt`).

Linux / macOS (single-config generator):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBONGO_CAT_FETCH_DEPS=ON
cmake --build build --parallel
```

Windows (from a Visual Studio 2022 developer shell; MSVC is required for
Cubism, MinGW is not supported for it):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBONGO_CAT_FETCH_DEPS=ON
cmake --build build --config Release --parallel
```

On Windows you can also use `build.bat` (wraps `scripts/build-windows.ps1`,
builds into `build-cubism/`, supports `-Clean` and `-Package` arguments and the
`BONGOCAT_BUILD_JOBS` / `BONGOCAT_REQUIRE_CUBISM` / `BONGOCAT_CLEAN_BUILD`
environment variables).

Executable output paths:
- Linux: `build/BongoCat`
- macOS: `build/BongoCat.app/Contents/MacOS/BongoCat`
- Windows (Visual Studio): `build/Release/BongoCat.exe`

Without a Cubism SDK the build succeeds but produces the diagnostic backend
(see `src/live2d/live2d_stub.c`); it starts and renders platform diagnostics
but does not render Live2D models.

### Cubism SDK

To build the full runtime, place a Cubism SDK for Native at
`vendor/CubismSdkForNative` or pass `-DBONGO_CAT_CUBISM_SDK=/path/to/...`.
`-DBONGO_CAT_REQUIRE_CUBISM=ON` makes configuration fail instead of silently
selecting the diagnostic backend. Windows Cubism builds require MSVC.

### CMake Options

| Option | Default | Description |
| --- | --- | --- |
| `BONGO_CAT_FETCH_DEPS` | `ON` | Download pinned third-party dependencies via FetchContent. |
| `BONGO_CAT_CUBISM_SDK` | `vendor/CubismSdkForNative` | Path to the Cubism SDK for Native. |
| `BONGO_CAT_REQUIRE_CUBISM` | `OFF` | Fail configuration when a usable Cubism SDK is unavailable. |
| `BONGO_CAT_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors (CI builds warning-clean). |

## Tests

CTest targets are enabled by default. Always build first, then run:

```bash
ctest --test-dir build --output-on-failure
# Visual Studio multi-config:
ctest --test-dir build -C Release --output-on-failure
```

Test executables (defined in `cmake/Tests.cmake`): `core`, `i18n`, `ui`,
`app-state`, `model-import-unit`; plus `live2d-motion-state` when Cubism is
enabled and `windows-capture` on Windows. Tests live under `tests/`, grouped
by area (`core/`, `i18n/`, `ui/`, `model_import/`, `live2d/`, `platform/`).

To run a single test: `ctest --test-dir build -R core --output-on-failure`
(`-R` matches test names).

## Mandatory Validation Gates

These are part of the `ALL` build target (see CMakeLists.txt) and also run in
CI (`ci.yml`). Any code change must keep them passing:

1. **`check-lines`** (`cmake/CheckLines.cmake`) - no source file under `src/`,
   `include/`, `tests/`, `cmake/`, or the root `CMakeLists.txt` may exceed
   **280 lines**. When a file grows past the limit, split it instead of
   squeezing logic together.
2. **`check-localization`** (`cmake/CheckLocalization.cmake`) - every i18n key
   used in source (quoted keys such as `"native.*"`, `"pages.*"`,
   `"components.*"`, `"composables.*"`) must exist in **every** locale JSON in
   `resources/assets/locales/`. When adding a UI string, add the key to all
   locale files in the same change.
3. **`check-platform-runtime-safety`** (`cmake/CheckPlatformRuntimeSafety.cmake`)
   - production code (everything under `src/` except `src/tools/`) must not
   call forbidden APIs (process injection, raw input injection, service
   control, ptrace equivalents, etc.). Sensitive capabilities are restricted
   to specific owner files listed in `SENSITIVE_RULES` (e.g. `SetWindowsHookEx`
   only in `src/platform/windows/windows_input.c`, `CGEventTapCreate` only in
   `src/platform/macos/macos_input.m`). Never move or duplicate these calls
   into other files; add a new owner entry only with clear justification.
4. **`check-cubism-user-model-safety`**
   (`cmake/CheckCubismUserModelSafety.cmake`) - guards how user models are
   handled in Cubism paths.

CI (`.github/workflows/ci.yml`) additionally runs Cppcheck, rejects legacy
product-name references, builds warning-clean on Linux, and builds/tests/
packages on Windows, macOS, and Linux.

## Architecture

Entry point: `src/main.c` -> `bongo_cat_app_run` (lifecycle in
`src/runtime/lifecycle/`). One `BongoCatApp` per process, one main-thread
SDL3 event + render loop.

```
Platform listeners (keyboard / pointer)      [out of main loop]
        |  atomic edge queue + coalesced pointer slot + SDL wake event
        v
main thread: dispatch SDL events, drain input queue, update state
        v
model parameters -> Live2D C ABI -> OpenGL composition -> platform presentation
```

Key rule: **platform listeners stop at the input boundary**. Native hooks
(Windows low-level hooks, macOS Quartz event tap, Linux XInput2) only publish
timestamped edges/pointer positions to the atomic input state
(`src/core/input_state.c`) and push a wake event. They never call Live2D,
overlay, or UI code directly. Keep this boundary when modifying input code.

Directory map:

| Path | Purpose |
| --- | --- |
| `include/bongo_cat/` | Public C API headers (the ABI between the C runtime and the C++ Live2D bridge). |
| `src/core/` | C11 foundation: config, i18n, input state, model catalog, paths, JSON, shortcuts, SHA-256, update checks. |
| `src/runtime/` | Application runtime: `lifecycle/` (startup, loop, shutdown), `input/` (gamepad, mouse, shortcuts), `model/` (catalogs, multi-pet, behavior), `model/import/` (Mver / Tauri / nearby model import), `shell/` (window, tray, menus), `update/`, `diagnostics/`. |
| `src/live2d/` | Live2D bridge: C ABI implementation on Cubism (C++17, only here) and `live2d_stub.c` diagnostic fallback. Cubism types stay behind opaque C handles. |
| `src/platform/` | Per-platform backends: `windows/`, `macos/`, `linux/`, plus `common/`. Presenters, input listeners, layered windows. |
| `src/media/` | Image/audio loading (stb, miniaudio). |
| `src/render/` | GL API helpers and overlay rendering. |
| `src/ui/` | Nuklear-based preferences window: `backend/`, `rendering/`, `theme/`, `preferences/`. Separate SDL/OpenGL window from the pet window. |
| `src/tools/` | Windows-only validation tools (desktop capture, blind tests, metrics); excluded from safety policy. |
| `tests/` | CTest executables grouped by area. |
| `resources/assets/` | Bundled models (`standard`, `keyboard`, `gamepad`), locales, icons. |
| `cmake/` | Build modules: dependencies, Cubism, sources lists, validation checks, packaging, tests. |

Model packages: **Mver is the canonical format**. Tauri packages are converted
to Mver on import; nearby sources are inspected without installing (cached
under `cache_root`, not `models_root`).

## Code Conventions

- Language standards: C11 (no extensions) and C++17 (only for Cubism bridge
  code in `src/live2d/`). MSVC additionally requires
  `/experimental:c11atomics` for C files using C11 atomics (already wired up
  in CMake for existing files; add it for new files with atomics, see
  `CMakeLists.txt` and `cmake/Tests.cmake`).
- Formatting: `.editorconfig` - UTF-8, LF line endings, final newline, trimmed
  trailing whitespace.
- Warnings: build with `-Wall -Wextra` (MSVC `/W4`). CI builds warning-clean;
  prefer keeping new code warning-free.
- The 280-line-per-file limit is enforced; keep new files small and focused.
- New C/C++ source files must be added to the source lists in `cmake/`
  (`RuntimeSources.cmake`, `Tests.cmake`, or `CMakeLists.txt` for core) - the
  build does not glob.
- Translations: any new UI string needs a key in all
  `resources/assets/locales/*.json` files or `check-localization` fails.
- Licensing: source is AGPL-3.0-only; bundled model assets in
  `resources/assets/models/{standard,keyboard,gamepad}` are MIT-licensed. Do
  not mix asset code into source files or vice versa.

## Useful Targets

- `cmake --build build --target check-lines` (and `check-localization`,
  `check-platform-runtime-safety`, `check-cubism-user-model-safety`) - run a
  single validation gate.
- `bongo_cat_validation_tools` (Windows only) - builds diagnostic viewer/metric
  tools under `src/tools/`.

## Additional Notes

- `docs/` contains translated READMEs; when changing user-facing build/usage
  instructions, update `README.md` and the translations where feasible.
- Windows shell scripting uses PowerShell (`scripts/`, `.github/scripts/`);
  keep batch files thin wrappers like `build.bat`.
