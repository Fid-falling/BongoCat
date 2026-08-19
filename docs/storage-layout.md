# Storage Layout

BongoCat owns its application data independently of both supported source
formats. Tauri Live2D and Bongo-Cat-Mver are model import formats only.

## Ownership

```text
config/settings.json       long-lived user preferences
config/settings.json.rejected* preserved invalid settings
data/models/               installed model package v2 directories
cache/embedded-assets-<version>/ extracted packaged application assets
cache/model-adapters/      generated adapters for nearby Tauri/Mver sources
state/session.json         disposable window and active-model state
state/session.json.rejected* preserved invalid session state
state/                     diagnostic and smoke-run artifacts
logs/                      startup and failure logs
```

`settings.json` uses format `bongocat/settings`, schema 1. It contains rendering
and pointer preferences, window behavior, application preferences, shortcuts,
behavior overrides, model display-name overrides, and an `extensions` object.
Unknown namespaces inside `extensions` are preserved across load and save; the
serialized object is limited to 4095 bytes so settings memory remains bounded.
The complete settings or session document is limited to 1 MiB.

`session.json` uses format `bongocat/session`, schema 1. It contains only the
active model ID and window visibility, scale, opacity, optional known position,
and size.
Deleting it must not change the user's preferences or installed models.

Every user-selected Tauri or Mver source is converted into an application-owned
package v2 directory under `data/models/`.

For portable Tauri and Mver distributions, the application also performs a
bounded scan next to the executable. These nearby sources remain externally
owned and are loaded in place; only their generated runtime adapters are stored under
`cache/model-adapters/`. The scan is limited to three directory levels, 256
directories, and 500 ms of directory-discovery work. Unchanged sources reuse a
metadata signature and cached content identity, so rescans do not rehash model
files or regenerate input images. Set `BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN` for
isolated development runs that must ignore adjacent sources.

```json
{
  "format": "bongocat/settings",
  "schemaVersion": 1,
  "rendering": {
    "modelMirrored": false,
    "pointerMirrored": false,
    "centerPointerTracking": true,
    "ignorePointerInput": false,
    "maximumFps": 60
  },
  "window": {
    "clickThrough": false,
    "alwaysOnTop": true,
    "hideOnPointerOver": false,
    "keepOnScreen": false,
    "captureBackground": false,
    "captureBackgroundColor": "#00ff00",
    "hideDelaySeconds": 0.0
  },
  "application": {
    "launchAtLogin": false,
    "showTrayIcon": true,
    "theme": "auto",
    "language": "en-US"
  },
  "shortcuts": {
    "toggleVisibility": "",
    "openSettings": "",
    "toggleModelMirror": "",
    "toggleClickThrough": "",
    "toggleAlwaysOnTop": ""
  },
  "behaviorOverrides": [],
  "modelOverrides": [],
  "extensions": {}
}
```

```json
{
  "format": "bongocat/session",
  "schemaVersion": 1,
  "window": {
    "visible": true,
    "scalePercent": 100.0,
    "opacityPercent": 100.0,
    "position": { "x": 0, "y": 0 },
    "size": { "width": 612, "height": 354 }
  },
  "activeModelId": "standard"
}
```

`position` is omitted until the application has observed a real window
position. Once present, `(0, 0)` is treated as an intentional location.

Both documents are written atomically through a temporary file, a durable
flush, and replacement. There are no readers or migrations for pre-release
configuration formats.

Schema 1 treats known fields strictly: an omitted field keeps its default, but
a present field with the wrong JSON type, an unsupported enum string, an
out-of-range machine integer, or an invalid nested structure rejects the whole
document. Duplicate known JSON keys are also rejected. Unknown fields outside
`extensions` are ignored and not preserved;
extension owners must store their data under a unique key in `extensions`.

Before saving, settings and session values are canonicalized. Numeric runtime
values are clamped to supported limits, non-finite values return to defaults,
invalid empty override rows are removed, and override IDs are unique. If an
override ID occurs more than once in memory or in a loaded document, later
non-empty values replace the corresponding earlier values.

Invalid or unsupported files are moved aside before defaults are saved. The
first rejected file uses the sibling name `settings.json.rejected` or
`session.json.rejected`; further files use `.rejected.1`, `.rejected.2`, and so
on. I/O and allocation failures leave the original in place and disable saving
that document for the current run. This prevents a transient failure or a file
that could not be preserved from being overwritten.

## Platform Roots

| Platform | Config and data | State, cache, and logs |
| --- | --- | --- |
| Windows | `%APPDATA%\BongoCat\config`; `%LOCALAPPDATA%\BongoCat\data` | `%LOCALAPPDATA%\BongoCat\state`, `cache`, `logs` |
| macOS | `~/Library/Application Support/BongoCat/config`, `data` | Application Support `state`; `~/Library/Caches/BongoCat`; `~/Library/Logs/BongoCat` |
| Linux | `$XDG_CONFIG_HOME/bongocat`, `$XDG_DATA_HOME/bongocat` | `$XDG_STATE_HOME/bongocat`, `$XDG_CACHE_HOME/bongocat`; logs under state |

Linux falls back to the corresponding `~/.config`, `~/.local/share`,
`~/.local/state`, and `~/.cache` roots when an XDG variable is unset.
Windows keeps only small preferences in the roaming profile; installed models
stay local so large packages are not copied by profile synchronization.
Application data is file-backed on every platform. Windows does not use the
registry for settings or autostart; enabling autostart creates a shortcut in
the current user's Startup folder.

`--storage-root=<path>` is the only path override. It creates `config`, `data`,
`cache`, `state`, and `logs` beneath the supplied root for isolated development
or CI runs.
