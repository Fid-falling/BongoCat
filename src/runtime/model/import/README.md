# Model import architecture

The installed package format is Mver. Other source formats must be converted
to an Mver package before they enter the installed-model catalog.

## Pipeline

1. `model_import_source.c` resolves the selected file or directory.
2. `model_import_discover.c` selects the source-format discovery module.
3. Identity and digest modules decide whether the package is already known.
4. Mver sources are copied while distribution-only files are filtered out.
5. Tauri sources are converted by the isolated `tauri/` module.
6. The prepared package is discovered again through the Mver module.
7. Mver metadata and assets are converted into the runtime adapter.

## Ownership

- Root files: shared workflow, storage, identity, scanning, and validation.
- `mver/`: canonical package discovery, copying, and runtime adaptation.
- `tauri/`: Tauri discovery and conversion to canonical Mver structure.
- `nearby/`: non-installing discovery and adapter caching.

## Invariants

- Native Mver imports never call Tauri conversion code.
- Tauri conversion ends by producing a valid Mver directory tree.
- Installed packages are validated through Mver discovery after preparation.
- Format-specific APIs stay in their format directories.
- Shared path and manifest helpers stay format-neutral.
