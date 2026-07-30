param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [string]$ImportFixture = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\preference-interactions" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preference-interaction-audit-$PID"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatPreferenceAuditNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct Rect { public int Left, Top, Right, Bottom; }
}
'@
[void][BongoCatPreferenceAuditNative]::SetProcessDPIAware()

function Get-AppWindows([int]$ProcessId) {
    $windows = [Collections.Generic.List[object]]::new()
    [BongoCatPreferenceAuditNative]::EnumWindows({
        param($handle, $data)
        [uint32]$owner = 0
        [void][BongoCatPreferenceAuditNative]::GetWindowThreadProcessId(
            $handle, [ref]$owner)
        if ($owner -eq $ProcessId -and
            [BongoCatPreferenceAuditNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatPreferenceAuditNative+Rect]::new()
            if ([BongoCatPreferenceAuditNative]::GetWindowRect($handle, [ref]$rect)) {
                $width = $rect.Right - $rect.Left
                $height = $rect.Bottom - $rect.Top
                $windows.Add([pscustomobject]@{ Handle=$handle; Width=$width;
                    Height=$height; Area=$width*$height })
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $windows
}

function Wait-Preferences([Diagnostics.Process]$Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $live = Get-Process -Id $Process.Id -ErrorAction Stop
        $raw_handle = $live.MainWindowHandle
        if ($null -ne $raw_handle -and $raw_handle -ne 0) {
            [IntPtr]$handle = $raw_handle
            $rect = [BongoCatPreferenceAuditNative+Rect]::new()
            if ([BongoCatPreferenceAuditNative]::GetWindowRect(
                $handle, [ref]$rect) -and $rect.Right - $rect.Left -gt 700) {
                return [pscustomobject]@{ Handle=$handle;
                    Width=$rect.Right-$rect.Left; Height=$rect.Bottom-$rect.Top }
            }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for preferences window for process $($Process.Id)"
}

function Get-Rect([object]$Window) {
    $rect = [BongoCatPreferenceAuditNative+Rect]::new()
    [void][BongoCatPreferenceAuditNative]::GetWindowRect(
        $Window.Handle, [ref]$rect)
    return $rect
}

function Click-At([object]$Window, [int]$X, [int]$Y) {
    $rect = Get-Rect $Window
    [void][BongoCatPreferenceAuditNative]::SetForegroundWindow($Window.Handle)
    [void][BongoCatPreferenceAuditNative]::SetCursorPos(
        $rect.Left + $X, $rect.Top + $Y)
    [BongoCatPreferenceAuditNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [BongoCatPreferenceAuditNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 450
}

function Press-Key([byte]$VirtualKey) {
    [BongoCatPreferenceAuditNative]::keybd_event(
        $VirtualKey, 0, 0, [UIntPtr]::Zero)
    [BongoCatPreferenceAuditNative]::keybd_event(
        $VirtualKey, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 350
}

function Press-Shortcut([byte]$Modifier, [byte]$VirtualKey) {
    [BongoCatPreferenceAuditNative]::keybd_event(
        $Modifier, 0, 0, [UIntPtr]::Zero)
    Press-Key $VirtualKey
    [BongoCatPreferenceAuditNative]::keybd_event(
        $Modifier, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 350
}

function Save-Window([object]$Window, [string]$Name) {
    $rect = Get-Rect $Window
    $bitmap = [Drawing.Bitmap]::new($rect.Right - $rect.Left,
        $rect.Bottom - $rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { $printed = [BongoCatPreferenceAuditNative]::PrintWindow(
        $Window.Handle, $dc, 2) } finally { $graphics.ReleaseHdc($dc) }
    if (-not $printed) { throw "PrintWindow failed for $Name" }
    $path = Join-Path $OutputDir "$Name.png"
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Start-Page([int]$Page, [string]$ImportPath = "") {
    $data = Join-Path $OutputDir ("data-$Page-" + [DateTime]::UtcNow.Ticks)
    $arguments = @("--ci-smoke", "--ci-preferences",
        "--ci-preference-page=$Page", "--ci-language=zh-CN",
        "--ci-theme=light", "--ci-exit-ms=20000", "--data-root=$data")
    if ($ImportPath) { $arguments += "--ci-import=$ImportPath" }
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    $window = Wait-Preferences $process
    [void][BongoCatPreferenceAuditNative]::SetWindowPos($window.Handle,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    Start-Sleep -Milliseconds 400
    return [pscustomobject]@{ Process=$process; Window=$window }
}

$results = [Collections.Generic.List[object]]::new()
function Capture-Scenario([string]$Name, [int]$Page, [scriptblock]$Actions,
    [string]$ImportPath = "") {
    $run = Start-Page $Page $ImportPath
    try {
        & $Actions $run.Window
        $path = Save-Window $run.Window $Name
        $results.Add([pscustomobject]@{ Scenario=$Name; Passed=$true; Path=$path })
    } finally {
        if (-not $run.Process.HasExited) { Stop-Process -Id $run.Process.Id -Force }
        [void]$run.Process.WaitForExit(3000)
    }
    Start-Sleep -Milliseconds 1000
}

Capture-Scenario "behavior-open" 2 { param($window) Click-At $window 468 320 }
Capture-Scenario "behavior-expression" 2 { param($window)
    Click-At $window 468 320; Click-At $window 585 252 }
Capture-Scenario "behavior-escape" 2 { param($window)
    Click-At $window 468 320; Press-Key 0x1b }
Capture-Scenario "shortcut-recorded" 3 { param($window)
    Click-At $window 758 141; Press-Shortcut 0x11 0x4b }
Capture-Scenario "shortcut-cleared" 3 { param($window)
    Click-At $window 758 141; Press-Shortcut 0x11 0x4b
    Click-At $window 832 141 }
Capture-Scenario "language-changed" 1 { param($window)
    Click-At $window 765 350; Click-At $window 765 435 }
Capture-Scenario "language-open" 1 { param($window) Click-At $window 765 350 }
Capture-Scenario "toggle-changed" 0 { param($window) Click-At $window 820 141 }
Capture-Scenario "stepper-changed" 0 { param($window) Click-At $window 833 293 }
Capture-Scenario "slider-changed" 0 { param($window) Click-At $window 700 388 }
Capture-Scenario "behavior-outside-close" 2 { param($window)
    Click-At $window 468 320; Click-At $window 170 100 }
Capture-Scenario "update-toast" 4 { param($window) Click-At $window 559 359 }
if ($ImportFixture) {
    Capture-Scenario "delete-confirm" 2 {
        param($window) Click-At $window 598 558
    } ([IO.Path]::GetFullPath($ImportFixture))
}

$report = Join-Path $OutputDir "audit.csv"
$results | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $report
Write-Output "Captured $($results.Count) preference interactions; report: $report"
