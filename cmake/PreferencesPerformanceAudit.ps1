param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\preferences-performance" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir "data"
New-Item -ItemType Directory -Force -Path $data | Out-Null
[IO.File]::WriteAllText((Join-Path $data "preferences.json"),
    '{"format":"bongo-cat/preferences","version":2,"app":{"trayVisible":true},"window":{"passThrough":true},"model":{"maxFPS":1}}')
[IO.File]::WriteAllText((Join-Path $data "session.json"),
    '{"format":"bongo-cat/session","version":2,"window":{"visible":false}}')

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatPreferencePerformanceNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatPreferencePerformanceNative]::SetProcessDPIAware()
$script:UiScale = 1.0
function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $path = Join-Path $data "preferences-window.txt"
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'handle=(\d+)') {
            $handle = [IntPtr][long]$Matches[1]
            [uint32]$owner = 0
            [void][BongoCatPreferencePerformanceNative]::GetWindowThreadProcessId($handle,
                [ref]$owner)
            $rect = [BongoCatPreferencePerformanceNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatPreferencePerformanceNative]::GetClientRect($handle,
                    [ref]$rect)) {
                return $handle
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}
function Get-VisibleWindowCount([int]$ProcessId) {
    $script:visibleWindowCount = 0
    [BongoCatPreferencePerformanceNative]::EnumWindows({
        param($handle, $unused)
        [uint32]$owner = 0
        [void][BongoCatPreferencePerformanceNative]::GetWindowThreadProcessId(
            $handle, [ref]$owner)
        if ($owner -eq $ProcessId -and
            [BongoCatPreferencePerformanceNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatPreferencePerformanceNative+Rect]::new()
            if ([BongoCatPreferencePerformanceNative]::GetWindowRect(
                $handle, [ref]$rect) -and $rect.R -gt $rect.L -and
                $rect.B -gt $rect.T) { $script:visibleWindowCount++ }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $script:visibleWindowCount
}

function Wait-UiScale([string]$Path) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $content = Get-Content -Raw -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($content -match 'layout_scale=([0-9.]+)') {
            return [double]::Parse($Matches[1],
                [Globalization.CultureInfo]::InvariantCulture)
        }
        Start-Sleep -Milliseconds 30
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for preferences scale metrics"
}

function Get-Point([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = [BongoCatPreferencePerformanceNative+Point]::new()
    $point.X = [int][Math]::Round($X * $script:UiScale)
    $point.Y = [int][Math]::Round($Y * $script:UiScale)
    [void][BongoCatPreferencePerformanceNative]::ClientToScreen($Window, [ref]$point)
    return $point
}

function Move-Pointer([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-Point $Window $X $Y
    [void][BongoCatPreferencePerformanceNative]::SetCursorPos($point.X, $point.Y)
}

function Click-Point([IntPtr]$Window, [double]$X, [double]$Y) {
    $clientX = [int][Math]::Round($X * $script:UiScale)
    $clientY = [int][Math]::Round($Y * $script:UiScale)
    $point = Get-Point $Window $X $Y
    $clip = [BongoCatPreferencePerformanceNative+Rect]::new()
    $clip.L = $point.X; $clip.T = $point.Y
    $clip.R = $point.X + 1; $clip.B = $point.Y + 1
    $packed = [IntPtr](($clientX -band 0xffff) -bor
        (($clientY -band 0xffff) -shl 16))
    [void][BongoCatPreferencePerformanceNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatPreferencePerformanceNative]::SetCursorPos(
            $point.X, $point.Y)
        [void][BongoCatPreferencePerformanceNative]::SendMessageW(
            $Window, 0x0200, [UIntPtr]::Zero, $packed)
        [void][BongoCatPreferencePerformanceNative]::SendMessageW(
            $Window, 0x0201, [UIntPtr]::new(1), $packed)
        Start-Sleep -Milliseconds 5
        [void][BongoCatPreferencePerformanceNative]::SendMessageW(
            $Window, 0x0202, [UIntPtr]::Zero, $packed)
    } finally {
        [void][BongoCatPreferencePerformanceNative]::ReleaseCursor([IntPtr]::Zero)
    }
}

function Select-Page([IntPtr]$Window, [string]$Frame, [double]$X,
    [double]$Y, [int]$Page) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        Click-Point $Window $X $Y
        $deadline = [DateTime]::UtcNow.AddSeconds(1)
        do {
            $content = Get-Content -Raw -LiteralPath $Frame `
                -ErrorAction SilentlyContinue
            if ($content -match 'page=(\d+)' -and [int]$Matches[1] -eq $Page) {
                return
            }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
    }
    throw "Preferences did not switch to page $Page"
}

function Save-Screen([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatPreferencePerformanceNative+Rect]::new()
    [void][BongoCatPreferencePerformanceNative]::GetWindowRect($Window, [ref]$rect)
    $bitmap = [Drawing.Bitmap]::new($rect.R - $rect.L, $rect.B - $rect.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Measure-Difference([string]$First, [string]$Second) {
    $a = [Drawing.Bitmap]::new($First); $b = [Drawing.Bitmap]::new($Second)
    try {
        $changed = 0; $samples = 0
        $step = [Math]::Max(1, [int][Math]::Round(4 * $script:UiScale))
        $top = [int][Math]::Round(60 * $script:UiScale)
        $left = [int][Math]::Round(155 * $script:UiScale)
        $right = [Math]::Min($a.Width, $b.Width) - $step
        $bottom = [Math]::Min($a.Height, $b.Height) - $step
        for ($y = $top; $y -lt $bottom; $y += $step) {
            for ($x = $left; $x -lt $right; $x += $step) {
                if ($a.GetPixel($x, $y).ToArgb() -ne $b.GetPixel($x, $y).ToArgb()) {
                    $changed++
                }
                $samples++
            }
        }
        return $changed / [double]$samples
    } finally { $a.Dispose(); $b.Dispose() }
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-performance-audit-$PID"
$arguments = @("--autostart", "--ci-smoke", "--ci-preferences", "--ci-preference-page=0",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-ignore-global-input",
    "--ci-exit-ms=40000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    $script:UiScale = Wait-UiScale (Join-Path $data "ui-frame.txt")
    $window = Wait-Preferences $process.Id
    [void][BongoCatPreferencePerformanceNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatPreferencePerformanceNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 600
    $baseline = Save-Screen $window "page-before.png"
    $process.Refresh(); $activeStart = $process.TotalProcessorTime
    $activeWatch = [Diagnostics.Stopwatch]::StartNew()
    Move-Pointer $window 833 293
    Start-Sleep -Milliseconds 25
    Select-Page $window (Join-Path $data "ui-frame.txt") 82 283 1
    Start-Sleep -Milliseconds 250
    Select-Page $window (Join-Path $data "ui-frame.txt") 82 353 2
    Start-Sleep -Milliseconds 250
    Select-Page $window (Join-Path $data "ui-frame.txt") 82 283 1
    Start-Sleep -Milliseconds 400
    $process.Refresh(); $activeCpu =
        ($process.TotalProcessorTime - $activeStart).TotalMilliseconds
    $activeWatch.Stop()
    $activeCorePercent = 100.0 * $activeCpu /
        $activeWatch.Elapsed.TotalMilliseconds
    $activePercent = $activeCorePercent / [Environment]::ProcessorCount
    $final = Save-Screen $window "page-after.png"
    for ($index = 0; $index -lt 24; $index++) {
        $width = [int][Math]::Round((720 + ($index % 8) * 32) * $script:UiScale)
        $height = [int][Math]::Round((560 + ($index % 6) * 18) * $script:UiScale)
        [void][BongoCatPreferencePerformanceNative]::SetWindowPos($window,
            [IntPtr](-1), 40, 40, $width, $height, 0x0040)
        Start-Sleep -Milliseconds 25
    }
    Start-Sleep -Milliseconds 300
    $frameContent = Get-Content -Raw -LiteralPath (Join-Path $data "ui-frame.txt")
    if ($frameContent -notmatch 'valid=1') {
        throw "Preferences frame became invalid during resize"
    }
    $paintMatch = [regex]::Match($frameContent,
        'paint_textures=(\d+) paint_bytes=(\d+)')
    if (-not $paintMatch.Success) {
        throw "Preferences paint cache metrics were not reported"
    }
    $paintTextures = [int]$paintMatch.Groups[1].Value
    $paintMiB = [double]$paintMatch.Groups[2].Value / 1MB
    $client = [BongoCatPreferencePerformanceNative+Rect]::new()
    [void][BongoCatPreferencePerformanceNative]::GetClientRect(
        $window, [ref]$client)
    Move-Pointer $window ($client.R / $script:UiScale - 20) `
        ($client.B / $script:UiScale - 20)
    $visibleWindows = Get-VisibleWindowCount $process.Id
    $framePath = Join-Path $data "ui-frame.txt"
    for ($settle = 0; $settle -lt 8; $settle++) {
        $frameWriteBefore = (Get-Item -LiteralPath $framePath).LastWriteTimeUtc.Ticks
        Start-Sleep -Milliseconds 1500
        if ((Get-Item -LiteralPath $framePath).LastWriteTimeUtc.Ticks -eq $frameWriteBefore) { break }
    }
    if ($settle -eq 8) { throw "Preferences did not settle before idle measurement" }
    $idleCpu = 0.0; $idleElapsed = 0.0; $idleCoreSamples = @(); $idleFrameChanges = 0
    for ($sample = 0; $sample -lt 3; $sample++) {
        $process.Refresh(); $idleStart = $process.TotalProcessorTime
        $frameWriteBefore = (Get-Item -LiteralPath $framePath).LastWriteTimeUtc.Ticks
        $idleWatch = [Diagnostics.Stopwatch]::StartNew()
        Start-Sleep -Milliseconds 2000
        $process.Refresh(); $idleWatch.Stop()
        if ((Get-Item -LiteralPath $framePath).LastWriteTimeUtc.Ticks -ne $frameWriteBefore) {
            $idleFrameChanges++ }
        $sampleCpu = ($process.TotalProcessorTime - $idleStart).TotalMilliseconds
        $sampleElapsed = $idleWatch.Elapsed.TotalMilliseconds
        $idleCpu += $sampleCpu; $idleElapsed += $sampleElapsed
        $idleCoreSamples += 100.0 * $sampleCpu / $sampleElapsed
    }
    $sortedIdle = @($idleCoreSamples | Sort-Object)
    $idleCorePercent = $sortedIdle[1]
    $idlePercent = $idleCorePercent / [Environment]::ProcessorCount
    $result = [ordered]@{
        PageDifference = Measure-Difference $baseline $final
        ActiveCpuMilliseconds = $activeCpu
        ActiveCpuPercent = $activePercent
        ActiveCorePercent = $activeCorePercent
        IdleCpuMilliseconds = $idleCpu
        IdleCpuPercent = $idlePercent
        IdleCorePercent = $idleCorePercent
        IdleCoreSamples = $idleCoreSamples
        IdleMeasurementMilliseconds = $idleElapsed
        IdleFrameChanges = $idleFrameChanges
        VisibleWindows = $visibleWindows
        PaintTextures = $paintTextures
        PaintMiB = $paintMiB
    }
    $result.Passed = $result.PageDifference -gt 0.02 -and
        $result.ActiveCorePercent -gt 2.0 -and $result.IdleCpuPercent -lt 2.0 -and
        $result.ActiveCorePercent -gt 2.0 * $result.IdleCorePercent -and
        $result.IdleFrameChanges -le 1 -and $result.VisibleWindows -eq 1 -and
        $result.PaintTextures -le 48 -and $result.PaintMiB -le 32.0
    $result | ConvertTo-Json | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
