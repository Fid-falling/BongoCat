# Storage Layout

BongoCat owns its application data independently of both supported source
formats. Tauri Live2D and Bongo-Cat-Mver are model import formats only.

## Ownership

```text
config/settings.json       long-lived user preferences
data/models/               installed model package v2 directories
cache/model-adapters/      generated adapters for nearby Mver folders
state/session.json         disposable window and active-model state
state/                     diagnostic and smoke-run artifacts
logs/                      startup and failure logs
```

`settings.json` uses format `bongocat/settings`, schema 1. It contains rendering
and pointer preferences, window behavior, application preferences, shortcuts,
behavior overrides, model display-name overrides, and an `extensions` object.
Unknown namespaces inside `extensions` are preserved across load and save; the
serialized object is limited to 4095 bytes so settings memory remains bounded.

`session.json` uses format `bongocat/session`, schema 1. It contains only the
active model ID and window visibility, scale, opacity, optional known position,
and size.
Deleting it must not change the user's preferences or installed models.

```json
{
  "format": "bongocat/settings",
  "schemaVersion": 1,
  "rendering": {
    "modelMirrored": false,
    "pointerMirrored": false,
    "centerPointerTracking": true,
    "ignorePointerInput": false,
    "inputReleaseDelaySeconds": 3.0,
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

`--storage-root=<path>` is the only path override. It creates `config`, `data`,
`cache`, `state`, and `logs` beneath the supplied root for isolated development
or CI runs.
