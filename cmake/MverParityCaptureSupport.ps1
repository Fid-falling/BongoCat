Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MverParityInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr GetPropW(IntPtr window, string name);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(IntPtr destination, int x, int y,
        int width, int height, IntPtr source, int sourceX, int sourceY,
        uint operation);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, int dx, int dy,
        uint data, UIntPtr extra);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte key, byte scan,
        uint flags, UIntPtr extra);
}
'@

[MverParityInput]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
[MverParityInput]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$mverDir = Join-Path $OutputDir "mver"
$nativeDir = Join-Path $OutputDir "native"
New-Item -ItemType Directory -Force -Path $mverDir,$nativeDir | Out-Null
if (@(Get-ChildItem -LiteralPath $mverDir,$nativeDir -File |
    Where-Object { $_.Extension -in @(".png", ".bmp") }).Count) {
    throw "Capture frame directories must be empty: $OutputDir"
}
$mver = Get-Process -Id $MverPid
$native = Get-Process -Id $NativePid
if (-not $mver.MainWindowHandle -or -not $native.MainWindowHandle) {
    throw "Both applications must have a top-level model window"
}
$nativeProxy = [MverParityInput]::GetPropW(
    $native.MainWindowHandle, "BongoCat.LayeredProxy")
if ($nativeProxy -eq [IntPtr]::Zero -or
    -not [MverParityInput]::IsWindowVisible($nativeProxy)) {
    throw "Native parity capture requires BongoCat --ci-pass-through"
}

function Get-Rect([IntPtr]$Window) {
    $rect = [MverParityInput+Rect]::new()
    if (-not [MverParityInput]::GetWindowRect($Window, [ref]$rect)) {
        throw "Cannot read model window bounds"
    }
    return $rect
}

function Set-CaptureWindowPosition([IntPtr]$Window, [int]$X, [int]$Y,
    [int]$Width, [int]$Height, [string]$Label) {
    if (-not [MverParityInput]::SetWindowPos($Window, [IntPtr](-1),
        $X, $Y, $Width, $Height, 0x40)) {
        throw "Cannot move $Label capture window; use an asInvoker Mver build"
    }
    Start-Sleep -Milliseconds 150
    $rect = Get-Rect $Window
    if ([Math]::Abs($rect.Left - $X) -gt 2 -or
        [Math]::Abs($rect.Top - $Y) -gt 2) {
        throw "$Label capture window rejected positioning; use an asInvoker Mver build"
    }
    return $rect
}

function Capture([IntPtr]$Window) {
    [Windows.Forms.Application]::DoEvents()
    $proxy = [MverParityInput]::GetPropW($Window, "BongoCat.LayeredProxy")
    $print = $proxy -ne [IntPtr]::Zero -and
        [MverParityInput]::IsWindowVisible($proxy)
    if ($print) { $Window = $proxy }
    $rect = Get-Rect $Window
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 64 -or $height -lt 64 -or $width -gt 4096 -or
        $height -gt 4096 -or [Math]::Abs($width - $height) -gt 2) {
        throw "Unexpected capture size ${width}x${height}"
    }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            if ($print) {
                $dc = $graphics.GetHdc()
                try {
                    if (-not [MverParityInput]::PrintWindow($Window, $dc, 2)) {
                        throw "PrintWindow failed for the Native layered presenter"
                    }
                } finally { $graphics.ReleaseHdc($dc) }
            } else {
                $destination = $graphics.GetHdc()
                $screen = [MverParityInput]::GetDC([IntPtr]::Zero)
                try {
                    if ($screen -eq [IntPtr]::Zero -or
                        -not [MverParityInput]::BitBlt($destination, 0, 0,
                            $width, $height, $screen, $rect.Left, $rect.Top,
                            0x40CC0020)) {
                        throw "Screen capture failed for the Mver reference"
                    }
                } finally {
                    if ($screen -ne [IntPtr]::Zero) {
                        [MverParityInput]::ReleaseDC([IntPtr]::Zero, $screen) |
                            Out-Null
                    }
                    $graphics.ReleaseHdc($destination)
                }
            }
            if ($width -ne $WindowSize -or $height -ne $WindowSize) {
                $normalized = [Drawing.Bitmap]::new($WindowSize, $WindowSize)
                $output = [Drawing.Graphics]::FromImage($normalized)
                try {
                    $output.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                    $output.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $output.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
                    $output.DrawImage($bitmap, 0, 0, $WindowSize, $WindowSize)
                } finally { $output.Dispose(); $bitmap.Dispose() }
                return $normalized
            }
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

function Assert-Foreground([Drawing.Bitmap]$Bitmap, [string]$Label) {
    $visible = 0
    for ($y = 0; $y -lt $Bitmap.Height; $y += 8) {
        for ($x = 0; $x -lt $Bitmap.Width; $x += 8) {
            $pixel = $Bitmap.GetPixel($x, $y)
            if ([Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)) -gt 12) {
                if ((++$visible) -ge 16) { return }
            }
        }
    }
    throw "$Label produced a blank first capture frame"
}

function Save-Pair([string]$Name) {
    [Windows.Forms.Application]::DoEvents()
    $mverBitmap = Capture $mver.MainWindowHandle
    $nativeBitmap = Capture $native.MainWindowHandle
    try {
        Assert-Foreground $mverBitmap "Mver frame $Name"
        Assert-Foreground $nativeBitmap "Native frame $Name"
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
