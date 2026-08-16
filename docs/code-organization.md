# Native code organization

The native implementation is organized by ownership. Directory names describe
the subsystem that owns a file; they are not a second public API. Public C
interfaces stay in `include/bongo_cat`, while headers under `src` are private
implementation details.

## Dependency direction

```text
include/bongo_cat
        ^
        |
src/core  <---  src/runtime/model/import
    ^              ^
    |              |
src/platform   src/runtime (lifecycle, input, shell)
        ^              ^
        +-------- src/ui (backend, rendering, preferences)
                         ^
                      src/main.c
```

The arrows describe allowed ownership, not a requirement that every module
depends on every module below it:

- `core` contains platform-neutral data and persistence primitives.
- `runtime/model` owns the catalog and storage model; `runtime/model/import`
  owns format adapters and nearby-package discovery.
- `runtime/lifecycle`, `runtime/input`, and `runtime/shell` coordinate the
  running application but do not define public data structures.
- `ui` consumes runtime state and renders it. UI code must not reach into a
  platform implementation through relative paths; private include directories
  are declared by CMake instead.
- `platform/<os>` owns native APIs. Cross-platform code calls public platform
  interfaces from `include/bongo_cat`.

## CMake ownership

`cmake/RuntimeSources.cmake` is the source-of-truth for runtime/UI ownership
groups. Platform-specific source and link settings live in
`cmake/PlatformRuntime.cmake`; validation executables, application resources,
and tests have their own target modules. This keeps the root `CMakeLists.txt`
focused on project-wide options and library wiring.

When adding a source file:

1. Put it in the smallest owning directory.
2. Add it to that directory's source group in `RuntimeSources.cmake`, or to the
   relevant platform/target module.
3. Add private include directories only to the target that needs them.
4. Run `cmake -DROOT=. -P cmake/CheckLines.cmake`, configure a clean build, and
   run `ctest --test-dir <build> --output-on-failure`.

Tests follow the same ownership model under `tests/core`, `tests/ui`,
`tests/model_import`, `tests/platform`, and `tests/i18n`. Fixtures stay under
`tests/fixtures` because they are data rather than implementation.
