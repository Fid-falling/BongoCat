param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\preferences-performance" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir "data"
New-Item -ItemType Directory -Force -Path $data | Out-Null
[IO.File]::WriteAllText((Join-Path $data "preferences.json"),
    '{"format":"bongo-cat-neo/preferences","version":1,"model":{"maxFPS":1}}')
[IO.File]::WriteAllText((Join-Path $data "session.json"),
    '{"format":"bongo-cat-neo/session","version":1,"window":{"visible":false}}')

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoPreferencePerformanceNative {
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
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatNeoPreferencePerformanceNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $script:found = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        [BongoCatNeoPreferencePerformanceNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatNeoPreferencePerformanceNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            if ($owner -eq $ProcessId -and
                [BongoCatNeoPreferencePerformanceNative]::IsWindowVisible($handle)) {
                $rect = [BongoCatNeoPreferencePerformanceNative+Rect]::new()
                if ([BongoCatNeoPreferencePerformanceNative]::GetWindowRect(
                    $handle, [ref]$rect) -and $rect.R - $rect.L -ge 700) {
                    $script:found = $handle
                }
            }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($script:found -ne [IntPtr]::Zero) { return $script:found }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Get-Point([IntPtr]$Window, [int]$X, [int]$Y) {
    $point = [BongoCatNeoPreferencePerformanceNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNeoPreferencePerformanceNative]::ClientToScreen($Window, [ref]$point)
    return $point
}

function Move-Pointer([IntPtr]$Window, [int]$X, [int]$Y) {
    $point = Get-Point $Window $X $Y
    [void][BongoCatNeoPreferencePerformanceNative]::SetCursorPos($point.X, $point.Y)
}

function Click-Point([IntPtr]$Window, [int]$X, [int]$Y) {
    Move-Pointer $Window $X $Y
    Start-Sleep -Milliseconds 75
    [BongoCatNeoPreferencePerformanceNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 35
    [BongoCatNeoPreferencePerformanceNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}

function Save-Screen([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatNeoPreferencePerformanceNative+Rect]::new()
    [void][BongoCatNeoPreferencePerformanceNative]::GetWindowRect($Window, [ref]$rect)
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
        for ($y = 60; $y -lt $a.Height - 10; $y += 4) {
            for ($x = 155; $x -lt $a.Width - 10; $x += 4) {
                if ($a.GetPixel($x, $y).ToArgb() -ne $b.GetPixel($x, $y).ToArgb()) {
                    $changed++
                }
                $samples++
            }
        }
        return $changed / [double]$samples
    } finally { $a.Dispose(); $b.Dispose() }
}

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "preferences-performance-audit-$PID"
$arguments = @("--autostart", "--ci-smoke", "--ci-preferences", "--ci-preference-page=0",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-ignore-global-input",
    "--ci-exit-ms=9000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatNeoPreferencePerformanceNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatNeoPreferencePerformanceNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 600
    $baseline = Save-Screen $window "page-before.png"
    $process.Refresh(); $activeStart = $process.TotalProcessorTime
    $activeWatch = [Diagnostics.Stopwatch]::StartNew()
    Move-Pointer $window 833 293
    Start-Sleep -Milliseconds 25
    Click-Point $window 82 283
    Start-Sleep -Milliseconds 400
    $process.Refresh(); $activeCpu =
        ($process.TotalProcessorTime - $activeStart).TotalMilliseconds
    $activeWatch.Stop()
    $activePercent = 100.0 * $activeCpu / $activeWatch.Elapsed.TotalMilliseconds
    $final = Save-Screen $window "page-after.png"
    Move-Pointer $window 880 650
    Start-Sleep -Milliseconds 700
    $process.Refresh(); $idleStart = $process.TotalProcessorTime
    $idleWatch = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Milliseconds 1500
    $process.Refresh(); $idleWatch.Stop()
    $idleCpu = ($process.TotalProcessorTime - $idleStart).TotalMilliseconds
    $idlePercent = 100.0 * $idleCpu / $idleWatch.Elapsed.TotalMilliseconds
    $result = [ordered]@{
        PageDifference = Measure-Difference $baseline $final
        ActiveCpuMilliseconds = $activeCpu
        ActiveCpuPercent = $activePercent
        IdleCpuMilliseconds = $idleCpu
        IdleCpuPercent = $idlePercent
    }
    $result.Passed = $result.PageDifference -gt 0.02 -and
        $result.ActiveCpuPercent -gt 2.0 -and $result.IdleCpuPercent -lt 2.0 -and
        $result.ActiveCpuPercent -gt 2.0 * $result.IdleCpuPercent
    $result | ConvertTo-Json | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
