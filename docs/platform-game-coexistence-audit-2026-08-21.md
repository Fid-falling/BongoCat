# Platform game coexistence audit - 2026-08-21

## Scope and outcome

This audit covers the Windows, macOS, and Linux production runtimes, platform
permissions, window integration, autostart paths, packaging, and validation
tools. Its goal is to minimize behavior that can resemble game manipulation
while retaining the global input and overlay features required by a desktop
pet.

BongoCat does not inject code, access another process's memory, install a
driver or privileged service, request debug privileges, or simulate keyboard
or mouse input. Platform-specific global input remains read-only and platform
windows are never attached to a game window.

No source audit can guarantee that every third-party anti-cheat product will
avoid a false positive. Signing, release reputation, dependency behavior, and
each anti-cheat vendor's policy remain external factors. macOS and Linux were
audited from source on a Windows host; real-machine verification remains a
release gate.

## Windows

The diagnostic run confirmed that an unelevated process does not receive
keyboard edges from an elevated Task Manager through `WH_KEYBOARD_LL`,
`GetAsyncKeyState`, or background Raw Input. This is a Windows integrity
boundary, not a model or input-queue defect. Input resumes when a
normal-integrity window becomes foreground.

The rejected workarounds are elevation, `uiAccess=true`, direct HID access, a
keyboard driver, process or DLL injection, and cross-process memory access.
The temporary Raw Input experiment, full virtual-key polling, and diagnostic
logging were removed. Retained physical-state polling only releases keys that
the normal hook previously saw go down.

The production executable remains `asInvoker` with `uiAccess=false`. Low-level
keyboard and mouse hooks only enqueue BongoCat events, DirectInput opens only
`GUID_SysMouse` as nonexclusive background input, and the layered proxy belongs
to BongoCat. Window activation no longer attaches BongoCat's GUI input queue to
the current foreground thread. Non-topmost desktop anchoring searches only the
Windows `Progman`/`WorkerW` shell windows.

The `BitBlt`/`PrintWindow` presentation probe runs only under the explicit
`--ci-smoke` path and receives only BongoCat-owned window handles. Normal
runtime does not capture the desktop or a game window, although those imports
can remain visible in the executable.

The NSIS update path uses only `SYNCHRONIZE |
PROCESS_QUERY_LIMITED_INFORMATION` after verifying the executable path. The
installer is current-user-only. Static SDL can retain ordinary Raw Input,
`DeviceIoControl`, and process imports for its gamepad, video, and process
backends. SDL's gamepad subsystem is initialized only while a gamepad model is
active; an import alone does not prove the application calls every API.

## macOS

Global input uses a `kCGSessionEventTap` at `kCGTailAppendEventTap` with
`kCGEventTapOptionListenOnly`. The callback observes key, button, and pointer
events, returns the original event unchanged, and cannot suppress or rewrite
input. BongoCat requests only Input Monitoring access. The unused Accessibility
usage declaration was removed.

The BongoCat-owned global keyboard and mouse path does not create or post
synthetic `CGEvent` input, use a HID-level event tap, request another process's
task port, use Mach process memory APIs, call `ptrace`, or install a privileged
helper. Static SDL can use the standard IOKit HID gamepad backend, but only
while a gamepad model is active. If macOS Secure Input or another security
control blocks observation, BongoCat accepts reduced input behavior instead of
bypassing it.

The floating, click-through, capture-sharing, tray, and activation operations
target BongoCat's own `NSWindow`. Single-instance wake-up uses an app-specific
distributed notification. Autostart writes a user-owned LaunchAgent under
`~/Library/LaunchAgents`; it does not use `SMJobBless` or a system daemon.

Input Monitoring is inherently a sensitive permission because it exposes
global key events. Developer ID signing, hardened runtime, notarization, and a
stable bundle identifier are required release controls, but a vendor can still
choose to restrict any process with a global event tap.

## Linux

The X11 path selects `XI_RawKey*`, `XI_RawButton*`, and `XI_RawMotion` events
from XInput2. It does not grab keys or buttons for global monitoring and does
not inject input. Pointer and keyboard grabs exist only while BongoCat's own
context menu is open and are released when the menu closes.

`XSendEvent` is confined to the Linux window-management modules and sends only
EWMH `ClientMessage` requests for activation, taskbar state, and moving the
BongoCat window. Project production source does not link XTest and contains no
`XTestFake*`, `XWarpPointer`, `/dev/uinput`, `ptrace`, process memory, setuid, or
Linux capability path. Static SDL can enumerate `/dev/input` and hidraw devices
and use `ioctl` for standard gamepad support, but that subsystem is initialized
only while a gamepad model is active. BongoCat never creates a virtual input
device or asks SDL to synthesize input. Click-through changes only the input
shape region of BongoCat's own X11 window.

Autostart writes a user desktop entry under `$XDG_CONFIG_HOME/autostart` or
`~/.config/autostart`. Directory opening executes `xdg-open` directly without
a shell. Single-instance activation reads the BongoCat lock file and sends a
window-manager activation request to the stored BongoCat window ID; it does not
access process memory.

Native Wayland intentionally has no global-input bypass. When no X11/XWayland
window or XInput2 is available, global input is reported unavailable rather
than falling back to portals, evdev, elevated access, or a helper daemon.

## Tool isolation

Input simulation and desktop-capture code exists only in
`src/tools/cubism_viewer_*_capture.c`. Those Windows validation executables are
`EXCLUDE_FROM_ALL`, are not linked into `BongoCat.exe`, and are not installed or
packaged with the application.

`cmake/CheckPlatformRuntimeSafety.cmake` enforces prohibited APIs and tokens,
platform permission boundaries, sensitive-module ownership, the reviewed SDL
source pin, and validation tool isolation. It scans project production source,
including Objective-C, but not fetched third-party source.

## Release requirements

- Rebuild release artifacts from clean source after diagnostic experiments are
  removed; never distribute a diagnostic executable.
- Authenticode-sign and timestamp the Windows executable and installer.
- Sign macOS with Developer ID, enable hardened runtime, notarize, and test the
  exact signed bundle because Input Monitoring consent is identity-sensitive.
- Sign Linux packages where the distribution format supports it, publish
  checksums, and keep artifacts traceable to a tagged source revision.
- Recheck final binary imports, linked libraries, signatures, and packaged file
  lists on every platform.
- Test alongside each supported anti-cheat on a non-production account. Vendor
  allow-listing or false-positive review is the only authoritative resolution
  for product-specific detection.

## Repeatable checks

- `cmake "-DROOT=$PWD" -P cmake/CheckPlatformRuntimeSafety.cmake`
- `cmake "-DROOT=$PWD" -P cmake/CheckLines.cmake`
- `git diff --check`
