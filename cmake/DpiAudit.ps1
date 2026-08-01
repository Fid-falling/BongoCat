param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [double[]]$Scales = @(1.0, 1.25, 1.5, 1.75, 2.0, 2.25)
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\dpi-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatDpiNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatDpiNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $found = [Collections.Generic.List[object]]::new()
        $script:visibleWindows = 0
        [BongoCatDpiNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatDpiNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatDpiNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatDpiNative]::IsWindowVisible($handle)) {
                $script:visibleWindows++
            }
            if ($owner -eq $ProcessId -and
                [BongoCatDpiNative]::IsWindowVisible($handle) -and
                [BongoCatDpiNative]::GetClientRect($handle, [ref]$rect) -and
                $rect.R -ge 400 -and $rect.B -ge 300) {
                $found.Add([pscustomobject]@{Handle=$handle;Area=$rect.R*$rect.B})
            }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($found.Count -and $script:visibleWindows -ge 2) {
            return ($found | Sort-Object Area -Descending | Select-Object -First 1).Handle
        }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Read-Metrics([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $text = Get-Content -Raw -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $text -or $text -notmatch 'valid=') { return $null }
    $values = @{}
    foreach ($match in [regex]::Matches($text, '(\w+)=([^\s]+)')) {
        $values[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    return $values
}

function Wait-Metrics([string]$Path, [int]$Page = -1, [int]$Seconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $metrics = Read-Metrics $Path
        if ($metrics -and ($Page -lt 0 -or [int]$metrics.page -eq $Page)) {
            return $metrics
        }
        Start-Sleep -Milliseconds 30
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for DPI frame metrics (page=$Page)"
}

function Number([string]$Value) {
    return [double]::Parse($Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Pair([string]$Value) {
    $parts = $Value -split 'x'
    return @((Number $parts[0]), (Number $parts[1]))
}

function Click-Support([IntPtr]$Window, [double]$Scale,
    [double]$LogicalWidth, [double]$LogicalHeight) {
    $header = if ($LogicalWidth -le 780.0) { 118.0 } else { 148.0 }
    $room = [Math]::Max(1.0, $LogicalHeight - 16.0 - $header)
    $top = [Math]::Min(16.0, [Math]::Max(4.0, $room * 0.05))
    $row = 68.0
    $gap = [Math]::Min(8.0, [Math]::Max(0.0,
        ($room - $top - $row * 5.0) / 4.0))
    if ($top + $row * 5.0 -gt $room) {
        $row = [Math]::Max(40.0, ($room - $top) / 5.0)
    }
    $logicalX = if ($LogicalWidth -le 780.0) { 50.0 } else { 82.0 }
    $logicalY = 8.0 + $header + $row * 4.0 + $top + $gap * 4.0 +
        ($row - 1.0) * 0.5
    $x = [int][Math]::Round($logicalX * $Scale)
    $y = [int][Math]::Round($logicalY * $Scale)
    $screen = [BongoCatDpiNative+Point]::new()
    $screen.X = $x; $screen.Y = $y
    [void][BongoCatDpiNative]::ClientToScreen($Window, [ref]$screen)
    [void][BongoCatDpiNative]::SetForegroundWindow($Window)
    [void][BongoCatDpiNative]::SetCursorPos($screen.X, $screen.Y)
    Start-Sleep -Milliseconds 30
    $clip = [BongoCatDpiNative+Rect]::new()
    $clip.L = $screen.X; $clip.T = $screen.Y
    $clip.R = $screen.X + 1; $clip.B = $screen.Y + 1
    $packed = [IntPtr](($x -band 0xffff) -bor (($y -band 0xffff) -shl 16))
    [void][BongoCatDpiNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatDpiNative]::SendMessageW(
            $Window, 0x0200, [UIntPtr]::Zero, $packed)
        [void][BongoCatDpiNative]::SendMessageW(
            $Window, 0x0201, [UIntPtr]::new(1), $packed)
        Start-Sleep -Milliseconds 5
        [void][BongoCatDpiNative]::SetCursorPos($screen.X, $screen.Y)
        [void][BongoCatDpiNative]::SendMessageW(
            $Window, 0x0202, [UIntPtr]::Zero, $packed)
    } finally { [void][BongoCatDpiNative]::ReleaseCursor([IntPtr]::Zero) }
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$results = [Collections.Generic.List[object]]::new()
$previousWindowWidth = 0.0
$previousAtlasArea = 0.0
foreach ($scale in $Scales) {
    $tag = $scale.ToString('0.00', [Globalization.CultureInfo]::InvariantCulture)
    $data = Join-Path $OutputDir "data-$tag"
    New-Item -ItemType Directory -Force -Path $data | Out-Null
    $frame = Join-Path $data "ui-frame.txt"
    Remove-Item -LiteralPath $frame -Force -ErrorAction SilentlyContinue
    $env:BONGO_CAT_TEST_UI_SCALE = $tag
    $env:BONGO_CAT_TEST_INSTANCE_ID = "dpi-audit-$tag-$PID"
    $arguments = @("--ci-smoke", "--ci-preferences", "--ci-preference-page=0",
        "--ci-language=zh-CN", "--ci-theme=light", "--ci-input-audit",
        "--ci-exit-ms=30000",
        "--data-root=$data")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    try {
        $window = Wait-Preferences $process.Id
        $metrics = Wait-Metrics $frame
        $window = Wait-Preferences $process.Id
        [void][BongoCatDpiNative]::SetForegroundWindow($window)
        Start-Sleep -Milliseconds 500
        $windowSize = Pair $metrics.window
        $pixelSize = Pair $metrics.pixels
        $logicalSize = Pair $metrics.logical
        $atlasSize = Pair $metrics.atlas
        $fonts = @($metrics.fonts -split ',' | ForEach-Object { Number $_ })
        $rect = [BongoCatDpiNative+Rect]::new()
        [void][BongoCatDpiNative]::GetClientRect($window, [ref]$rect)
        $atlasArea = $atlasSize[0] * $atlasSize[1]
        $passed = [int]$metrics.valid -eq 1 -and [int]$metrics.gl_error -eq 0 -and
            [Math]::Abs((Number $metrics.layout_scale) - $scale) -lt 0.01 -and
            [Math]::Abs((Number $metrics.raster_scale) - $scale) -lt 0.01 -and
            [Math]::Abs($windowSize[0] / $scale - $logicalSize[0]) -lt 1.1 -and
            [Math]::Abs($windowSize[1] / $scale - $logicalSize[1]) -lt 1.1 -and
            [Math]::Abs($rect.R - $windowSize[0]) -le 2 -and
            [Math]::Abs($rect.B - $windowSize[1]) -le 2 -and
            $pixelSize[0] -eq $windowSize[0] -and $pixelSize[1] -eq $windowSize[1] -and
            $logicalSize[0] -le 900.5 -and $logicalSize[1] -le 680.5 -and
            $logicalSize[0] -ge 400.0 -and $logicalSize[1] -ge 300.0 -and
            $fonts.Count -eq 5 -and $fonts[0] -eq 18.0 -and
            $fonts[1] -eq 20.0 -and $fonts[2] -eq 20.0 -and
            $fonts[3] -eq 28.0 -and $fonts[4] -eq 36.0 -and
            $windowSize[0] -ge $previousWindowWidth -and
            $atlasArea -ge $previousAtlasArea
        $after = $null
        for ($attempt = 0; $attempt -lt 3; $attempt++) {
            $window = Wait-Preferences $process.Id
            Click-Support $window $scale $logicalSize[0] $logicalSize[1]
            try { $after = Wait-Metrics $frame 4 4; break }
            catch { if ($attempt -eq 2) { throw } }
        }
        $passed = $passed -and [int]$after.valid -eq 1
        $result = [pscustomobject]@{ Scale=$scale; Window=$metrics.window;
            Pixels=$metrics.pixels; Logical=$metrics.logical; Atlas=$metrics.atlas;
            Fonts=$metrics.fonts; SupportClickable=([int]$after.page -eq 4);
            Passed=$passed }
        $results.Add($result)
        $previousWindowWidth = $windowSize[0]
        $previousAtlasArea = $atlasArea
    } finally {
        if ($process -and -not $process.HasExited) {
            $process.Kill(); $process.WaitForExit()
        }
    }
}
Remove-Item Env:BONGO_CAT_TEST_UI_SCALE -ErrorAction SilentlyContinue
$passed = @($results | Where-Object { -not $_.Passed }).Count -eq 0
[ordered]@{ Results=$results; Passed=$passed } | ConvertTo-Json -Depth 4 |
    Set-Content -Encoding utf8 (Join-Path $OutputDir "result.json")
$results | Format-Table -AutoSize
if (-not $passed) { exit 1 }
