param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\preferences-navigation" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir "data"
New-Item -ItemType Directory -Force -Path $data | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNavigationNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr handle);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr handle, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct Rect { public int Left, Top, Right, Bottom; }
    public struct Point { public int X, Y; }
}
'@
[void][BongoCatNavigationNative]::SetProcessDPIAware()
$script:UiScale = 1.0

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $path = Join-Path $data "preferences-window.txt"
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'handle=(\d+)') {
            $handle = [IntPtr][long]$Matches[1]
            [uint32]$owner = 0
            [void][BongoCatNavigationNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatNavigationNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatNavigationNative]::GetClientRect($handle, [ref]$rect)) {
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

function Move-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    $X = [int][Math]::Round($X * $script:UiScale)
    $Y = [int][Math]::Round($Y * $script:UiScale)
    $point = [BongoCatNavigationNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNavigationNative]::ClientToScreen($Window, [ref]$point)
    [void][BongoCatNavigationNative]::SetCursorPos($point.X, $point.Y)
}

function Click-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    Move-Client $Window $X $Y
    Start-Sleep -Milliseconds 30
    $X = [int][Math]::Round($X * $script:UiScale)
    $Y = [int][Math]::Round($Y * $script:UiScale)
    $point = [BongoCatNavigationNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNavigationNative]::ClientToScreen($Window, [ref]$point)
    $clip = [BongoCatNavigationNative+Rect]::new()
    $clip.Left = $point.X; $clip.Top = $point.Y
    $clip.Right = $point.X + 1; $clip.Bottom = $point.Y + 1
    $packed = [IntPtr](($X -band 0xffff) -bor (($Y -band 0xffff) -shl 16))
    [void][BongoCatNavigationNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0200, [UIntPtr]::Zero, $packed)
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0201, [UIntPtr]::new(1), $packed)
        Start-Sleep -Milliseconds 5
        Move-Client $Window $X $Y
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0202, [UIntPtr]::Zero, $packed)
    } finally { [void][BongoCatNavigationNative]::ReleaseCursor([IntPtr]::Zero) }
}

function Read-Pixel([IntPtr]$Window, [int]$X, [int]$Y) {
    $X = [int][Math]::Round($X * $script:UiScale)
    $Y = [int][Math]::Round($Y * $script:UiScale)
    $point = [BongoCatNavigationNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNavigationNative]::ClientToScreen($Window, [ref]$point)
    $desktop = [IntPtr]::Zero
    $dc = [BongoCatNavigationNative]::GetDC($desktop)
    try { return [BongoCatNavigationNative]::GetPixel($dc, $point.X, $point.Y) }
    finally { [void][BongoCatNavigationNative]::ReleaseDC($desktop, $dc) }
}

function Test-Pink([uint32]$Color) {
    $red = $Color -band 255
    $green = ($Color -shr 8) -band 255
    $blue = ($Color -shr 16) -band 255
    return $red -ge 220 -and $green -ge 80 -and $green -le 170 -and $blue -ge 120
}

function Save-Client([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatNavigationNative+Rect]::new()
    [void][BongoCatNavigationNative]::GetClientRect($Window, [ref]$rect)
    $point = [BongoCatNavigationNative+Point]::new()
    [void][BongoCatNavigationNative]::ClientToScreen($Window, [ref]$point)
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
    $point = [BongoCatNavigationNative+Point]::new()
    $nativeX = [int][Math]::Round(82 * $script:UiScale)
    $nativeY = [int][Math]::Round($Y * $script:UiScale)
    $point.X = $nativeX; $point.Y = $nativeY
    [void][BongoCatNavigationNative]::ClientToScreen($Window, [ref]$point)
    $clip = [BongoCatNavigationNative+Rect]::new()
    $clip.Left = $point.X; $clip.Top = $point.Y
    $clip.Right = $point.X + 1; $clip.Bottom = $point.Y + 1
    $packed = [IntPtr](($nativeX -band 0xffff) -bor
        (($nativeY -band 0xffff) -shl 16))
    [void][BongoCatNavigationNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0200, [UIntPtr]::Zero, $packed)
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0201, [UIntPtr]::new(1), $packed)
        Start-Sleep -Milliseconds 5
        [void][BongoCatNavigationNative]::SetCursorPos($point.X, $point.Y)
        [void][BongoCatNavigationNative]::SendMessageW(
            $Window, 0x0202, [UIntPtr]::Zero, $packed)
    } finally { [void][BongoCatNavigationNative]::ReleaseCursor([IntPtr]::Zero) }
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

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-navigation-audit-$PID"
$arguments = @("--ci-preferences", "--ci-frame-series", "--ci-preference-page=0",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-input-audit", "--ci-exit-ms=15000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatNavigationNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatNavigationNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 500
    $window = Wait-Preferences $process.Id
    $client = [BongoCatNavigationNative+Rect]::new()
    [void][BongoCatNavigationNative]::GetClientRect($window, [ref]$client)
    $script:UiScale = $client.Right / 900.0
    $logicalHeight = $client.Bottom / $script:UiScale
    $room = [Math]::Max(1.0, $logicalHeight - 16.0 - 148.0)
    $top = [Math]::Min(16.0, [Math]::Max(4.0, $room * .05))
    $row = 68.0
    $gap = [Math]::Min(8.0, [Math]::Max(0.0,
        ($room - $top - $row * 5.0) / 4.0))
    if ($top + $row * 5.0 -gt $room) {
        $row = [Math]::Max(40.0, ($room - $top) / 5.0)
    }
    $centers = @(0..4 | ForEach-Object {
        [int][Math]::Round(8.0 + 148.0 + $row * $_ + $top +
            $gap * $_ + ($row - 1.0) * .5) })
    $navigation = foreach ($page in @(4, 3, 2, 1, 0)) {
        Click-Client $window 82 $centers[$page]
        Start-Sleep -Milliseconds 280
        Save-Client $window "page-$page"
        $color = Read-Pixel $window 120 $centers[$page]
        [pscustomobject]@{ Page=$page; CenterY=$centers[$page];
            Color=("0x{0:X6}" -f ($color -band 0xffffff)); Passed=(Test-Pink $color) }
    }
    $framePath = Join-Path $data "preferences-frames.csv"
    $attempts = [Collections.Generic.List[object]]::new()
    $selected = $null
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $frameStart = if (Test-Path -LiteralPath $framePath) {
            @(Get-Content -LiteralPath $framePath).Count } else { 0 }
        $target = if (($attempt % 2) -eq 0) { 1 } else { 0 }
        $candidateTransition = Measure-Transition $window $centers[$target]
        $candidateFrames = Measure-Frames $framePath $frameStart
        $candidatePassed = $candidateTransition.Changes -ge 3 -and
            $candidateTransition.SettledMs -le 320 -and
            $candidateFrames.Frames -ge 8 -and
            $candidateFrames.AverageMs -le 20 -and
            $candidateFrames.P90Ms -le 32 -and $candidateFrames.MaxMs -le 50
        $candidate = [pscustomobject]@{ Attempt=$attempt + 1; Page=$target;
            Transition=$candidateTransition; Frames=$candidateFrames;
            Passed=$candidatePassed }
        $attempts.Add($candidate)
        if ($candidatePassed) { $selected = $candidate; break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $selected) { $selected = $attempts[$attempts.Count - 1] }
    $transition = $selected.Transition; $frames = $selected.Frames
    $passed = @($navigation | Where-Object { -not $_.Passed }).Count -eq 0 -and
        $selected.Passed
    $result = [ordered]@{ Navigation=$navigation; Transition=$transition;
        Frames=$frames; Attempts=$attempts; Passed=$passed }
    $result | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    $navigation | Format-Table
    $transition | Format-List
    $frames | Format-List
    if (-not $passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
