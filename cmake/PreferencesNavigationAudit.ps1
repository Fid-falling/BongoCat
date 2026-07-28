param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\preferences-navigation" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir "data"
New-Item -ItemType Directory -Force -Path $data | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoNavigationNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr handle);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr handle, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct Rect { public int Left, Top, Right, Bottom; }
    public struct Point { public int X, Y; }
}
'@
[void][BongoCatNeoNavigationNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $found = [Collections.Generic.List[object]]::new()
        [BongoCatNeoNavigationNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatNeoNavigationNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatNeoNavigationNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatNeoNavigationNative]::IsWindowVisible($handle) -and
                [BongoCatNeoNavigationNative]::GetClientRect($handle, [ref]$rect) -and
                $rect.Right -ge 700) { $found.Add($handle) }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($found.Count) { return $found[0] }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Move-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    $point = [BongoCatNeoNavigationNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNeoNavigationNative]::ClientToScreen($Window, [ref]$point)
    [void][BongoCatNeoNavigationNative]::SetCursorPos($point.X, $point.Y)
}

function Click-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    Move-Client $Window $X $Y
    Start-Sleep -Milliseconds 30
    [BongoCatNeoNavigationNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 20
    [BongoCatNeoNavigationNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}

function Read-Pixel([IntPtr]$Window, [int]$X, [int]$Y) {
    $point = [BongoCatNeoNavigationNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNeoNavigationNative]::ClientToScreen($Window, [ref]$point)
    $desktop = [IntPtr]::Zero
    $dc = [BongoCatNeoNavigationNative]::GetDC($desktop)
    try { return [BongoCatNeoNavigationNative]::GetPixel($dc, $point.X, $point.Y) }
    finally { [void][BongoCatNeoNavigationNative]::ReleaseDC($desktop, $dc) }
}

function Test-Pink([uint32]$Color) {
    $red = $Color -band 255
    $green = ($Color -shr 8) -band 255
    $blue = ($Color -shr 16) -band 255
    return $red -ge 220 -and $green -ge 80 -and $green -le 170 -and $blue -ge 120
}

function Save-Client([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatNeoNavigationNative+Rect]::new()
    [void][BongoCatNeoNavigationNative]::GetClientRect($Window, [ref]$rect)
    $point = [BongoCatNeoNavigationNative+Point]::new()
    [void][BongoCatNeoNavigationNative]::ClientToScreen($Window, [ref]$point)
    $bitmap = [Drawing.Bitmap]::new($rect.Right, $rect.Bottom)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($point.X, $point.Y, 0, 0, $bitmap.Size)
    $path = Join-Path $OutputDir "$Name.png"
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}

function Measure-Transition([IntPtr]$Window, [int]$Y) {
    Move-Client $Window 82 $Y
    Start-Sleep -Milliseconds 30
    $before = Read-Pixel $Window 120 $Y
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [BongoCatNeoNavigationNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 12
    [BongoCatNeoNavigationNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    $changes = [Collections.Generic.List[double]]::new()
    $last = $before
    while ($watch.ElapsedMilliseconds -lt 360) {
        $current = Read-Pixel $Window 120 $Y
        if ($current -ne $last) { $changes.Add($watch.Elapsed.TotalMilliseconds); $last = $current }
        Start-Sleep -Milliseconds 1
    }
    $watch.Stop()
    $maxGap = 0.0
    for ($i = 1; $i -lt $changes.Count; $i++)
        { $maxGap = [Math]::Max($maxGap, $changes[$i] - $changes[$i - 1]) }
    return [pscustomobject]@{ Changes=$changes.Count; ChangeTimesMs=@($changes);
        MaxGapMs=$maxGap;
        SettledMs=$(if ($changes.Count) { $changes[$changes.Count - 1] } else { 999.0 }) }
}

function Measure-Frames([string]$Path, [int]$Start) {
    $ticks = if (Test-Path -LiteralPath $Path) {
        @(Get-Content -LiteralPath $Path | ForEach-Object { [uint64]$_ })
    } else { @() }
    $samples = @($ticks | Select-Object -Skip $Start)
    $intervals = for ($i = 1; $i -lt $samples.Count; $i++) {
        ($samples[$i] - $samples[$i - 1]) / 1000000.0
    }
    $sorted = @($intervals | Sort-Object)
    $p90 = if ($sorted.Count) {
        $sorted[[Math]::Ceiling(($sorted.Count - 1) * .90)]
    } else { 999.0 }
    $stats = $intervals | Measure-Object -Average -Maximum
    return [pscustomobject]@{ Frames=$samples.Count; AverageMs=$(if ($stats.Count) {
        $stats.Average } else { 999.0 }); P90Ms=$p90;
        MaxMs=$(if ($stats.Count) { $stats.Maximum } else { 999.0 }) }
}

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "preferences-navigation-audit-$PID"
$arguments = @("--ci-preferences", "--ci-frame-series", "--ci-preference-page=0",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-exit-ms=15000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatNeoNavigationNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatNeoNavigationNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 500
    $centers = @(207, 283, 359, 435, 511)
    $navigation = foreach ($page in @(4, 3, 2, 1, 0)) {
        Click-Client $window 82 $centers[$page]
        Start-Sleep -Milliseconds 280
        Save-Client $window "page-$page"
        $color = Read-Pixel $window 120 $centers[$page]
        [pscustomobject]@{ Page=$page; CenterY=$centers[$page];
            Color=("0x{0:X6}" -f ($color -band 0xffffff)); Passed=(Test-Pink $color) }
    }
    $framePath = Join-Path $data "preferences-frames.csv"
    $frameStart = if (Test-Path -LiteralPath $framePath) {
        @(Get-Content -LiteralPath $framePath).Count } else { 0 }
    $transition = Measure-Transition $window $centers[1]
    $frames = Measure-Frames $framePath $frameStart
    $passed = @($navigation | Where-Object { -not $_.Passed }).Count -eq 0 -and
        $transition.Changes -ge 3 -and $transition.SettledMs -le 320 -and
        $frames.Frames -ge 8 -and $frames.AverageMs -le 20 -and
        $frames.P90Ms -le 25 -and $frames.MaxMs -le 30
    $result = [ordered]@{ Navigation=$navigation; Transition=$transition;
        Frames=$frames; Passed=$passed }
    $result | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    $navigation | Format-Table
    $transition | Format-List
    $frames | Format-List
    if (-not $passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
