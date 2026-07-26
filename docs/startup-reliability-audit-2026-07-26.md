# Startup and preferences reliability audit - 2026-07-26

## Scope and outcome

This audit covers the native release executable built on the available Windows
host, with emphasis on double-click startup, recovery from damaged user data,
preferences behavior, visual parity, resource stability and portable paths.
The audited binary is:

- `build-final/Release/BongoCatNeo.exe`
- Code commit: `a774faf`
- SHA-256: `F81B66008086CF0EFEAFFB2BDAD420F75966FBB4098C0B54513750E82EC9F487`
- Size: 5,280,768 bytes

The Windows release passed every automated test and UI audit listed below.
No finite test suite can prove that startup will succeed on every possible
machine, so signing, other Windows versions and real macOS/Linux machines remain
explicit release gates rather than being represented as completed work.

## Reliability corrections

| Failure class | Implemented protection |
|---|---|
| A second launch appeared to do nothing | A named wake event notifies the primary process, which restores, moves on-screen and raises its window. |
| Hidden or off-screen saved state | Startup recovers a visible on-screen main window when launched interactively. |
| Invalid or partially written settings | Strict JSON parsing rejects corruption and falls back to defaults; writes remain atomic. |
| Damaged extracted assets | Every embedded asset is checked by size and SHA-256 and the cache is repaired. |
| Damaged custom Live2D model | Model and texture validation occurs before install/load; a bundled model is used as fallback. |
| Unavailable global input hooks | Startup continues without global hooks and records an actionable diagnostic. |
| Unsupported multisampling | OpenGL context creation retries without MSAA. |
| Fatal graphics initialization | Startup stage and error logs are persisted instead of failing silently. |
| Deep or Unicode paths | UTF-8/UTF-16 path I/O and long-path-aware startup/import paths are used on Windows. |
| Large portable model trees | Discovery has depth, directory and time budgets and avoids recursive stack growth. |
| Interrupted model import | Temporary imports are isolated and stale residue is cleaned on the next startup. |

## Automated startup evidence

The MSVC/Cubism Release configuration passed 9/9 CTest cases: core, i18n,
Nuklear UI, app state, model-import unit tests, model format fixtures, optional
real-sample discovery, portable startup soak and the startup reliability matrix.

The startup matrix independently exercises:

- fresh extraction and a visible nonblank first frame with `gl_error=0`;
- same-size asset corruption and cache repair;
- malformed settings recovery;
- hidden and far off-screen state recovery;
- blocked global hooks and the no-MSAA OpenGL fallback;
- malformed model fallback;
- bounded portable-directory scanning;
- Unicode, spaces and paths longer than the legacy Windows `MAX_PATH` limit;
- long-path cache reuse, cleanup and model import;
- fatal OpenGL diagnostics; and
- second-instance wake-up both from initial hidden state and after closing a
  running window to the tray, without hanging either process.

The same source also completed a MinGW GCC 16.1 build with warnings treated as
errors and passed 9/9 tests. All 27 PowerShell audit scripts parse successfully,
`git diff --check` is clean and the 300-line source policy passes.

## Preferences verification

| Area | Result | Evidence |
|---|---|---|
| Visual matrix | PASS 50/50 | 5 pages x 5 locales x 2 themes |
| Main interactions | PASS | Toggle, scroll, navigation, combo, locale and shortcut editing |
| Persistence | PASS | Toggle and shortcut survive process restart |
| Dialog/state scenarios | PASS 7/7 | Behavior, expression, Escape, shortcut, locale, toggle and update toast |
| Pointer feedback | PASS 11/11 | Tabs, close, toggles, combo, shortcut, import/model cards and cancel |
| Localization fonts | PASS | Western base font plus merged CJK fallback; no missing glyphs in the matrix |

Primary retained artifacts are under `build-final`:

- `preferences-visual-matrix-current/audit.csv`
- `preferences-interaction-current/result.json`
- `preference-scenarios-current/audit.csv`
- `preferences-cursor-current/result.json`

## Performance and stability

Seven warm launches averaged 549 ms to the first valid frame, with a 592 ms
P95; the clean-cache launch reached its first valid frame in 1,030 ms.

| 60-second scenario | Peak working set | Peak private bytes | Final change | Handle change |
|---|---:|---:|---:|---:|
| Main window | 87.027 MiB | 64.461 MiB | Negative | +1 |
| Preferences open | 131.207 MiB | 104.539 MiB | About +0.8 MiB | +1 |

The preferences process has a separate 150/120 MiB working/private threshold
because it owns a second OpenGL drawable and a multilingual font atlas. The main
window retains the stricter 120/90 MiB threshold. Temporary font baking memory
is released after atlas upload, and opening preferences triggers allocator
cleanup. No unbounded growth was observed in either run.

## Windows release properties

- x64 GUI executable with the static MSVC runtime; imported DLLs are Windows
  system components only.
- ASLR, high-entropy VA, DEP/NX and Control Flow Guard are enabled.
- Stack reserve is 1 MiB; large startup catalogs and application state live on
  the heap.
- The executable is not Authenticode-signed. Signing is required before broad
  public distribution to reduce SmartScreen warnings and establish provenance.

## Cross-platform boundary

Platform I/O now uses shared bounded/atomic helpers, while Windows-specific
startup behavior remains isolated in the Windows platform layer. The repository
CI defines warnings-as-errors builds and native tests for Windows x64, Ubuntu
Linux x64 and macOS. A local Windows host cannot provide honest macOS/Linux GUI,
permission, compositor or packaging validation; those jobs and real-machine
smokes must pass for each release candidate.

The optional external real-model audit reports no configured samples on this
host. Bundled models and generated malformed fixtures are covered, but a curated
external model corpus remains a release gate.

## Repeatable commands

- `cmake --build build-final --config Release --parallel 2`
- `ctest --test-dir build-final -C Release --output-on-failure`
- `cmake -DROOT=. -P cmake/CheckLines.cmake`
- `powershell -NoProfile -ExecutionPolicy Bypass -File cmake/StartupPerformanceAudit.ps1 -Exe build-final/Release/BongoCatNeo.exe -OutputDir build-final/startup-performance-final`
- `powershell -NoProfile -ExecutionPolicy Bypass -File cmake/VisualAudit.ps1 -Exe build-final/Release/BongoCatNeo.exe -OutputDir build-final/preferences-visual-matrix-current -PreferencesMatrix`
