param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\slider-drag" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-$PID-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatSliderNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatSliderNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $path = Join-Path $data "preferences-window.txt"
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'handle=(\d+)') {
            $handle = [IntPtr][long]$Matches[1]
            [uint32]$owner = 0
            [void][BongoCatSliderNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatSliderNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatSliderNative]::GetClientRect($handle, [ref]$rect)) {
                return $handle
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    $evidence = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
    $startup = Get-Content -Tail 8 (Join-Path $data "startup.log") -ErrorAction SilentlyContinue
    if ($startup -match 'Missing OpenGL function:' -and $startup -match 'Preferences failed') {
        Write-Output "Preferences audit skipped: OpenGL shader APIs unavailable"
        exit 77
    }
    throw "Preferences window was not created: handle=[$evidence] startup=[$startup]"
}

function Get-ScreenPoint([IntPtr]$Window, [double]$X, [double]$Y) {
    $rect = [BongoCatSliderNative+Rect]::new()
    [void][BongoCatSliderNative]::GetClientRect($Window, [ref]$rect)
    $point = [BongoCatSliderNative+Point]::new()
    $point.X = [int][Math]::Round($X * $rect.R / 900.0)
    $point.Y = [int][Math]::Round($Y * $rect.B / 680.0)
    [void][BongoCatSliderNative]::ClientToScreen($Window, [ref]$point)
    return $point
}

function Move-To([BongoCatSliderNative+Point]$Point) {
    [void][BongoCatSliderNative]::SetCursorPos($Point.X, $Point.Y)
    Start-Sleep -Milliseconds 120
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-slider-drag-audit-$PID"
$arguments = @("--ci-preferences", "--ci-preference-page=0",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-exit-ms=15000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatSliderNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatSliderNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 500
    $window = Wait-Preferences $process.Id
    $start = Get-ScreenPoint $window 700 388
    $below = Get-ScreenPoint $window 700 500
    $outside = Get-ScreenPoint $window 930 500
    Move-To $start
    [BongoCatSliderNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 180
    Move-To $below
    Move-To $outside
    [BongoCatSliderNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 180
    Move-To (Get-ScreenPoint $window 620 388)
    Start-Sleep -Milliseconds 450
    [void][BongoCatSliderNative]::PostMessageW(
        $window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $sessionPath = Join-Path $data "session.json"
    for ($i = 0; $i -lt 60 -and -not (Test-Path $sessionPath); $i++) {
        Start-Sleep -Milliseconds 50
    }
    $opacity = [double](Get-Content -Raw -Encoding utf8 $sessionPath |
        ConvertFrom-Json).window.opacity
    $passed = [Math]::Abs($opacity - 100.0) -lt 0.01
    $result = [ordered]@{ Opacity=$opacity; Expected=100.0;
        LeftTrackWhilePressed=$true; ReleasedOutsideWindow=$true; Passed=$passed }
    $result | ConvertTo-Json | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $passed) { exit 1 }
} finally {
    [BongoCatSliderNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    if ($process -and -not $process.HasExited) {
        $process.Kill(); $process.WaitForExit()
    }
}
