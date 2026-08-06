# Native completion contract

The standalone native application is complete only when each automated check
and the matching manual scenario pass without depending on another frontend or
desktop runtime.

## Model and rendering

- Load all three bundled Cubism `model3` models.
- Import and remove custom model directories without changing their layout.
- Render drawable order, masks, blend modes, opacity, multiply and screen color.
- Run `motion3` motions, `exp3` expressions, fade timing, and motion audio.
- Preserve mouse, keyboard, and gamepad parameter mappings.
- Draw the model background and at most one key overlay per hand.
- Preserve mirror, mouse mirror, model scale, opacity, radius, and FPS controls.

## Input

- Observe global keyboard press and release events.
- Observe global mouse buttons and coalesced pointer movement.
- Preserve Windows automatic release timing for system keys.
- Support SDL game controllers, sticks, triggers, and hot plug.
- Register behavior shortcuts and application shortcuts.
- Request the required macOS input-monitoring permission.

## Window and application

- Transparent, borderless, draggable main window with per-pixel alpha.
- Click-through, always-on-top, hide-on-hover, and keep-in-monitor behavior.
- Shift plus secondary drag resizing from 10 through 500 percent.
- Tray and context menus, taskbar visibility, and single instance handling.
- Restore window position and size across monitor and DPI changes.
- Autostart, project information, and application exit.

## Preferences and data

- General, cat, model, behavior, and shortcut preference views.
- English, Simplified Chinese, Traditional Chinese, Portuguese, and Vietnamese.
- Light, dark, and system themes.
- Persist native settings and custom models without external project paths.
- Build, test, run, and package with only files contained in this repository.
- Persist settings atomically and recover from a truncated write.

## Release gates

- Golden images for idle, key, mouse, controller, motion, and expression states.
- Recorded input traces produce equivalent parameter values in old and new apps.
- Windows 10/11, macOS x64/arm64, and Linux X11 manual matrix passes.
- Wayland reports reduced global-input support instead of silently failing.
- Idle CPU is below 0.3 percent on the reference machine.
- Working set is below 100 MiB with one bundled model active.
- Hidden rendering is suspended and a 24-hour run has no unbounded growth.
- Every hand-written file is at most 300 physical lines.

## Mver visual equivalence

Mver 0.1.6 is the runtime behavior baseline for every model. Motion curve
evaluation, expression blending, breath, physics, eye blink, and pointer-driven
look updates do not switch algorithms by source format. A Tauri/standalone
Live2D model is imported through an adapter that preserves its native canvas
composition and `ParamMouse*` extension inputs; it does not select a second
animation runtime.

Mver imports retain the source package's `l2d_correct`, `window_size`,
`l2d_offset`, and horizontal-follow values in `.bongo-cat-mver.json`. The native
renderer uses the Mver 0.1.6 projection order for those models. Expressions keep
the authored canvas projection so the Live2D model remains aligned with the
background and input layers. The compatibility matrix also converts the
modern Cubism Core canvas units to Mver 0.1.6 units while retaining authored
Layout translation and `l2d_offset`. Its horizontal option controls following,
not model mirroring. Window resizing follows the imported reference aspect
ratio. Pointer-driven head, body, eye, and physics parameters use Mver's
configured `workarea`, or its DPI-virtualized primary-screen coordinate domain
when no custom work area is enabled.

All Mver models use Cubism `TargetPoint` for standard head, body, eye, and
physics response. Mver 0.1.6 does not directly write `ParamMouseX` or
`ParamMouseY`; the current-Core adapter supplies their complete authored
compatibility range when those channels exist, reproducing the old runtime's
observed hand and pen travel alongside `TargetPoint`. Standalone Tauri
models retain their full `ParamMouse*` convention through the adapter. The Mver
adapter also translates the horizontal target sign so the current Cubism
runtime reproduces Mver 0.1.6's observed on-screen look direction.

## Live2D texture quality

Live2D atlases retain their authored pixel dimensions for both bundled and
imported models. The loader only downsamples an atlas when it exceeds the
active GPU's `GL_MAX_TEXTURE_SIZE`, or retries at 2048 pixels after an explicit
`GL_OUT_OF_MEMORY` upload failure. Model textures use the same linear,
non-mipmapped sampling as Mver so changing the pet window size does not switch
filtering behavior or silently select a lower-resolution source.
