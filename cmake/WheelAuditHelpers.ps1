$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\wheel-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoWheelNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool PostMessageW(
        IntPtr h, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool GetLayeredWindowAttributes(
        IntPtr h, out uint color, out byte alpha, out uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(
        uint action, uint parameter, out Rect value, uint update);
    [DllImport("user32.dll")] public static extern IntPtr GetShellWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern void keybd_event(
        byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint x, uint y, int data, UIntPtr extraInfo);
}
'@

function Wait-Window([Diagnostics.Process]$Process) {
    for ($index = 0; $index -lt 100; $index++) {
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        Start-Sleep -Milliseconds 40
    }
    throw "Bongo Cat Neo window was not created"
}

function Get-Rect([IntPtr]$Window) {
    $rect = [BongoCatNeoWheelNative+Rect]::new()
    if (-not [BongoCatNeoWheelNative]::GetWindowRect($Window, [ref]$rect)) {
        throw "Cannot read Bongo Cat Neo window bounds"
    }
    return $rect
}

function Wheel-WParam([int]$Delta, [int]$Keys = 0) {
    return [IntPtr]([int64](($Delta -band 0xffff) -shl 16) -bor $Keys)
}

function Position-LParam($Rect) {
    $x = [int](($Rect.L + $Rect.R) / 2) -band 0xffff
    $y = [int](($Rect.T + $Rect.B) / 2) -band 0xffff
    return [IntPtr]([int64](($y -shl 16) -bor $x))
}

function Get-VisiblePixels([string]$Path) {
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        if (Test-Path -LiteralPath $Path) {
            try {
                $bitmap = [Drawing.Bitmap]::new($Path)
                $visible = 0
                for ($y = 0; $y -lt $bitmap.Height; $y += 4) {
                    for ($x = 0; $x -lt $bitmap.Width; $x += 4) {
                        $color = $bitmap.GetPixel($x, $y)
                        if ($color.R + $color.G + $color.B -gt 60) { $visible++ }
                    }
                }
                $bitmap.Dispose()
                return $visible
            } catch { Start-Sleep -Milliseconds 25 }
        } else { Start-Sleep -Milliseconds 25 }
    }
    return 0
}
