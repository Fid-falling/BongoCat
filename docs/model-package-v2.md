# BongoCat Model Package v2

## Purpose

BongoCat uses its own installed package format. Tauri Live2D folders,
Bongo-Cat-Mver packages, and Mver image patches are import formats, not runtime
storage formats.

The import boundary is:

```text
Tauri / Mver / Mver patch
             -> format importer
             -> one BongoCat package per mode
             -> normalized runtime adapter
             -> application runtime
```

This keeps the large Mver model ecosystem available without carrying Mver's
three-mode executable layout into BongoCat's long-term data model.

Bundled presets are read-only application assets and already follow the same
one-directory-per-mode runtime contract. The v2 disk manifest applies to
installed custom models; preset catalog entries receive equivalent synthetic
identity and capability fields without repackaging shipped assets.

## Directory Contract

```text
<package-id>/
  .bongo-cat-package.json
  payload/
  adapter/
    .bongo-cat-adapter.json
    .bongo-cat-import-report.json
    resources/
```

`payload/` preserves the source files required by this mode. It is never
modified by runtime code. `adapter/` contains normalized BongoCat metadata and
derived assets. A package is independently installable and removable; sibling
modes are related only by `familyId`.

For Mver imports, the original `config.json` may remain in `payload/` as source
provenance, while model and input-asset trees are copied only for the package's
mode. Neither loading nor removal depends on the other modes being installed.

## Manifest v2

```json
{
  "schemaVersion": 2,
  "packageId": "model-<sha256>-standard",
  "contentDigest": "<sha256>",
  "familyId": "family-<sha256>",
  "displayName": "Example - Standard",
  "mode": "standard",
  "source": {
    "format": "bongo-cat-mver",
    "name": "Example",
    "layout": "full-package",
    "preserved": true
  },
  "model": {
    "directory": "payload/img/standard/cat_model",
    "setting": "cat.model3.json"
  },
  "runtime": {
    "adapter": "adapter",
    "metadata": "adapter/.bongo-cat-adapter.json",
    "adapterSchemaVersion": 1,
    "generatorVersion": 1
  },
  "capabilities": [
    "live2d",
    "preview",
    "runtime-adapter",
    "input-images",
    "behaviors",
    "mver-projection",
    "pointer-overlay"
  ],
  "extensions": {
    "mver": {
      "configuration": "payload/config.json"
    }
  }
}
```

`packageId` is the permanent application-facing ID. For unmanifested source
folders it is derived from the full candidate content digest and mode. An exact
re-import therefore resolves to the existing package instead of creating a
timestamp duplicate. `contentDigest` is a lowercase SHA-256 over the selected
model, per-file content fingerprints, source adapter inputs, mode, and import
format. `familyId` is present when one discovery operation yields related modes.

The fields remain separate so a future signed or publisher-authored package can
keep a publisher ID across revisions while changing `contentDigest`. Raw folder
imports are intentionally content-addressed until such an identity exists.

Readers ignore unknown capabilities, source fields, and extension namespaces.
Relative paths must stay inside the package. Schema v1 remains readable and
keeps its directory-name ID; it is not rewritten or destructively migrated.
`adapterSchemaVersion` describes the runtime contract. `generatorVersion`
tracks the importer implementation so derived adapters can be rebuilt in place
after conversion behavior changes without modifying preserved payloads.
The catalog accepts a v2 package only when its adapter metadata has the declared
schema, runtime kind, source format, render profile, and bindings structure.

## Runtime Adapter

`.bongo-cat-adapter.json` is source-format neutral:

```json
{
  "schemaVersion": 1,
  "kind": "bongo-cat-runtime-adapter",
  "sourceFormat": "bongo-cat-mver",
  "render": { "profile": "mver-0.1.6" },
  "bindings": [],
  "standardPointer": {}
}
```

Tauri imports receive a minimal native adapter. Mver importers translate
render calibration, input bindings, pointer assets, motions, expressions,
effects, and audio defaults into this file. Runtime code does not read Mver
`config.json`. Existing `.bongo-cat-mver.json` adapters remain a read-only
fallback for installed v1 packages and nearby-model caches.

## Configuration Ownership

The package owns model-authored state: Live2D references, render calibration,
input asset mappings, pointer geometry, motions, expressions, effects, and
audio behavior.

Application configuration owns user state: selected package ID, window
geometry, scale, visibility, click-through behavior, always-on-top state, FPS,
mirror preferences, hover behavior, custom labels, and shortcut overrides.

Importers may read source application settings to reproduce model-authored
calibration, but they must not make source window or desktop settings part of
the user's BongoCat preferences.

## Evolution Rules

- Add optional fields or capability strings without changing `schemaVersion`.
- Increment the manifest schema only for incompatible structural changes.
- Keep format-specific data under `extensions.<format>`.
- Never require sibling modes for loading, updating, or removing a package.
- Never make runtime behavior depend directly on an import format's config.
- Preserve v1 readers until an explicit, reversible migration is shipped.
