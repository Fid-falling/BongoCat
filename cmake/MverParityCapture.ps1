param(
    [Parameter(Mandatory=$true)][int]$MverPid,
    [Parameter(Mandatory=$true)][int]$NativePid,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [int]$WindowSize = 428
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MverParityInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, int dx, int dy,
        uint data, UIntPtr extra);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte key, byte scan,
        uint flags, UIntPtr extra);
}
'@

[MverParityInput]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$mverDir = Join-Path $OutputDir "mver"
$nativeDir = Join-Path $OutputDir "native"
New-Item -ItemType Directory -Force -Path $mverDir,$nativeDir | Out-Null
$mver = Get-Process -Id $MverPid
$native = Get-Process -Id $NativePid
if (-not $mver.MainWindowHandle -or -not $native.MainWindowHandle) {
    throw "Both applications must have a top-level model window"
}

function Get-Rect([IntPtr]$Window) {
    $rect = [MverParityInput+Rect]::new()
    if (-not [MverParityInput]::GetWindowRect($Window, [ref]$rect)) {
        throw "Cannot read model window bounds"
    }
    return $rect
}

function Capture([IntPtr]$Window) {
    # Keep unrelated windows out of transparent pixels: backing form first,
    # then the captured window. Mouse-button cases can otherwise reorder them.
    [MverParityInput]::SetWindowPos($background.Handle, [IntPtr](-1),
        0, 0, 0, 0, 0x13) | Out-Null
    [MverParityInput]::SetWindowPos($Window, [IntPtr](-1),
        0, 0, 0, 0, 0x13) | Out-Null
    [Windows.Forms.Application]::DoEvents()
    $rect = Get-Rect $Window
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -ne $WindowSize -or $height -ne $WindowSize) {
        throw "Unexpected capture size ${width}x${height}"
    }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                [Drawing.Size]::new($width, $height))
            return $bitmap
        } catch {
            $bitmap.Dispose()
            if ($attempt -eq 3) { throw }
            Start-Sleep -Milliseconds 100
        } finally {
            $graphics.Dispose()
        }
    }
}

function Save-Pair([string]$Name) {
    [Windows.Forms.Application]::DoEvents()
    $mverBitmap = Capture $mver.MainWindowHandle
    $nativeBitmap = Capture $native.MainWindowHandle
    try {
        $mverBitmap.Save((Join-Path $mverDir "$Name.png"),
            [Drawing.Imaging.ImageFormat]::Png)
        $nativeBitmap.Save((Join-Path $nativeDir "$Name.png"),
            [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $mverBitmap.Dispose()
        $nativeBitmap.Dispose()
    }
}

function Move-Relative([int]$X, [int]$Y, [int]$Count = 1) {
    for ($index = 0; $index -lt $Count; $index++) {
        [MverParityInput]::mouse_event(1, $X, $Y, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 16
    }
}

function Shortcut([byte]$Key) {
    [MverParityInput]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [MverParityInput]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 35
    [MverParityInput]::keybd_event($Key, 0, 2, [UIntPtr]::Zero)
    [MverParityInput]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
}

$mverOriginal = Get-Rect $mver.MainWindowHandle
$nativeOriginal = Get-Rect $native.MainWindowHandle
$background = [Windows.Forms.Form]::new()
$background.FormBorderStyle = [Windows.Forms.FormBorderStyle]::None
$background.BackColor = [Drawing.Color]::Black
$background.StartPosition = [Windows.Forms.FormStartPosition]::Manual
$background.Bounds = [Windows.Forms.SystemInformation]::VirtualScreen
$background.ShowInTaskbar = $false
$background.TopMost = $true

try {
    $background.Show()
    [Windows.Forms.Application]::DoEvents()
    [MverParityInput]::SetWindowPos($mver.MainWindowHandle, [IntPtr](-1),
        140, 100, $WindowSize, $WindowSize, 0x40) | Out-Null
    [MverParityInput]::SetWindowPos($native.MainWindowHandle, [IntPtr](-1),
        700, 100, $WindowSize, $WindowSize, 0x40) | Out-Null
    Start-Sleep -Milliseconds 700

    Move-Relative -1200 -1200 4
    Start-Sleep -Milliseconds 1500
    Save-Pair "corner-tl-000"

    for ($frame = 1; $frame -le 40; $frame++) {
        Move-Relative 55 0
        Start-Sleep -Milliseconds 17
        Save-Pair ("sweep-x-{0:D3}" -f $frame)
    }
    Start-Sleep -Milliseconds 1000
    Save-Pair "corner-tr-000"

    Move-Relative -1200 1200 4
    Start-Sleep -Milliseconds 1200
    Save-Pair "corner-bl-000"
    for ($frame = 1; $frame -le 24; $frame++) {
        Move-Relative 95 -55
        Start-Sleep -Milliseconds 17
        Save-Pair ("sweep-diagonal-{0:D3}" -f $frame)
    }
    Start-Sleep -Milliseconds 1000
    Save-Pair "corner-tr-return-000"

    [MverParityInput]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    Save-Pair "mouse-left-down-000"
    [MverParityInput]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    Save-Pair "mouse-left-up-000"
    [MverParityInput]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    Save-Pair "mouse-right-down-000"
    [MverParityInput]::mouse_event(16, 0, 0, 0, [UIntPtr]::Zero)

    Shortcut 0x4C
    for ($frame = 1; $frame -le 12; $frame++) {
        Start-Sleep -Milliseconds 50
        Save-Pair ("expression-0-{0:D3}" -f $frame)
    }
    Shortcut 0x31
    for ($frame = 1; $frame -le 12; $frame++) {
        Start-Sleep -Milliseconds 50
        Save-Pair ("lock-motion-0-{0:D3}" -f $frame)
    }
} finally {
    [MverParityInput]::SetWindowPos($mver.MainWindowHandle, [IntPtr](-1),
        $mverOriginal.Left, $mverOriginal.Top,
        $mverOriginal.Right - $mverOriginal.Left,
        $mverOriginal.Bottom - $mverOriginal.Top, 0x40) | Out-Null
    [MverParityInput]::SetWindowPos($native.MainWindowHandle, [IntPtr](-1),
        $nativeOriginal.Left, $nativeOriginal.Top,
        $nativeOriginal.Right - $nativeOriginal.Left,
        $nativeOriginal.Bottom - $nativeOriginal.Top, 0x40) | Out-Null
    $background.Close()
    $background.Dispose()
}

$mverCount = @(Get-ChildItem -LiteralPath $mverDir -File).Count
$nativeCount = @(Get-ChildItem -LiteralPath $nativeDir -File).Count
if ($mverCount -ne $nativeCount -or $mverCount -lt 90) {
    throw "Incomplete parity capture: mver=$mverCount native=$nativeCount"
}
Write-Output "Captured $mverCount synchronized frame pairs"
