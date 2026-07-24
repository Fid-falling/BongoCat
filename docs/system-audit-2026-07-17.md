# Windows systematic audit - updated 2026-07-18

## Scope

This audit covers the independent native application on the available Windows
host: bundled assets, Cubism Native rendering, input states, pointer tracking,
model import, window chrome, preferences, localization, resource use, tests and
standalone desktop delivery. Non-Windows execution remains an external gate.

## Defects found and corrected

| Defect | Correction | Proof |
|---|---|---|
| Keyboard fallback retained raised-paw marks | Compose complete idle/input frames and erase the replaced paw area | Overlay audit |
| Main window exposed Win32 resize chrome | Clear caption, frame, menu and caption-button styles | Windows style audit |
| Shared audit data reused stale frames | Isolate runtime data and wait for completed frame files | Visual audit |
| External input contaminated internal scenarios | Ignore global input during deterministic internal scenarios | Exact release/idle frames |
| Texture replacement peaked at 689 MiB | Reuse one texture and coalesce transitions | 1000-transition stress |
| Diagnostic idle woke at 60 FPS | Wait 100 ms while SDL events still wake immediately | CPU soak |
| Cubism startup overflowed the Windows stack | Move the large application and behavior catalogs to the heap | Real Cubism smoke |
| Cubism retained a pointer to a local SDK option | Keep the Framework option alive for the full SDK lifetime | Real model startup |
| Cubism OpenGL calls inherited the overlay VAO | Unbind the overlay VAO after every direct draw | GL error 0 |
| Model textures rendered black | Generate mipmaps required by the Cubism minification filter | Real-model screenshots |
| Standalone exe could miss Framework shaders | Embed shaders and resolve them from packaged assets | Shader-free sibling smoke |

## Automated evidence

| Area | Result |
|---|---|
| Release build | PASS - MSVC v143, static CRT, Cubism SDK 5 r.5 |
| Native tests | PASS - 7/7 |
| Source policy | PASS - all checked files at most 300 lines |
| Preferences | PASS - 5 languages x 2 themes x 5 pages = 50/50 |
| Real models | PASS - standard, keyboard and gamepad |
| Live2D states | PASS - idle, keys, mouse buttons, gamepad, motions, expression and mirror |
| Pointer tracking | PASS - normal/mirrored frame differences and seven parameter assertions |
| OpenGL | PASS - framebuffer capture reports error 0 |
| Standalone packaging | PASS - one 6.99 MB exe extracts 22 shaders and renders Cubism |
| Working set | PASS - 57.32 MiB visible Cubism process |
| Desktop delivery | PASS - build and desktop SHA-256 match |

## Repeatable commands

- `ctest --test-dir build-cubism -C Release --output-on-failure`
- `cmake/AssetAudit.ps1`
- `cmake/OverlayAudit.ps1`
- `cmake/WindowsStyleAudit.ps1`
- `cmake/ThemeImportAudit.ps1`
- `cmake/VisualAudit.ps1`
- `cmake/SoakAudit.ps1`

## External gates

- Physical gamepad hot-plug and audible speaker confirmation.
- Uninterrupted 24-hour result and a second supported Windows version.
- macOS and Linux real-machine execution and signing.

No diagnostic fixed image is counted as a Cubism rendering pass.
