param(
    [string]$Exe = "",
    [string]$Model = "model-32027d288-standard-1",
    [int]$DurationMilliseconds = 12000,
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\focus-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
$frame = Join-Path $data "frame.bmp"
$inputAudit = Join-Path $data "input-audit.txt"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoFocusNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint x, uint y, uint data, UIntPtr extra);
}
'@

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "focus-audit-$PID"
$arguments = @("--ci-smoke", "--ci-input-audit", "--ci-exit-ms=$DurationMilliseconds",
    "--ci-model=$Model", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory `
    (Split-Path $Exe) -PassThru
try {
    for ($index = 0; $index -lt 120 -and -not (Test-Path $frame); ++$index) {
        Start-Sleep -Milliseconds 25
    }
    if (-not (Test-Path $frame)) { throw "bongo_cat_neo frame audit was not created" }
    $process.Refresh()
    $catWindow = $process.MainWindowHandle
    if ($catWindow -eq [IntPtr]::Zero) { throw "Bongo Cat window was not found" }
    $samples = [Collections.Generic.List[object]]::new()
    $started = [Diagnostics.Stopwatch]::StartNew()
    $explorer = Start-Process explorer.exe -ArgumentList (Get-Location) -PassThru
    Start-Sleep -Milliseconds 400
    $explorerWindow = Get-Process explorer -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1
    if ($explorerWindow) {
        [void][BongoCatNeoFocusNative]::SetForegroundWindow($explorerWindow.MainWindowHandle)
        Start-Sleep -Milliseconds 200
    }
    $inactiveBeforeDrag = [BongoCatNeoFocusNative]::GetForegroundWindow() -ne $catWindow
    $before = [BongoCatNeoFocusNative+Rect]::new()
    if (-not [BongoCatNeoFocusNative]::GetWindowRect($catWindow, [ref]$before)) {
        throw "Bongo Cat bounds were not available"
    }
    $startX = [int](($before.L + $before.R) / 2)
    $startY = [int](($before.T + $before.B) / 2)
    [void][BongoCatNeoFocusNative]::SetCursorPos($startX, $startY)
    [BongoCatNeoFocusNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    for ($step = 1; $step -le 5; $step++) {
        [void][BongoCatNeoFocusNative]::SetCursorPos($startX + 14 * $step,
            $startY + 8 * $step)
        Start-Sleep -Milliseconds 35
    }
    [BongoCatNeoFocusNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $after = [BongoCatNeoFocusNative+Rect]::new()
    [void][BongoCatNeoFocusNative]::GetWindowRect($catWindow, [ref]$after)
    $dragDeltaX = $after.L - $before.L
    $dragDeltaY = $after.T - $before.T
    $movedOnFirstPress = [Math]::Abs($dragDeltaX) -ge 20 -or
        [Math]::Abs($dragDeltaY) -ge 20
    while ($started.ElapsedMilliseconds -lt $DurationMilliseconds - 1000) {
        $last = (Get-Item $frame).LastWriteTimeUtc
        $samples.Add([pscustomobject]@{
            ElapsedMilliseconds=$started.ElapsedMilliseconds
            FrameUtc=$last.ToString("o")
        })
        Start-Sleep -Milliseconds 100
    }
    $unique = @($samples.FrameUtc | Select-Object -Unique).Count
    $inputLines = if (Test-Path $inputAudit) { @(Get-Content $inputAudit) } else { @() }
    $leftDown = @($inputLines | Where-Object { $_ -like "kind=3 name=Left*" }).Count
    $leftUp = @($inputLines | Where-Object { $_ -like "kind=4 name=Left*" }).Count
    $result = [ordered]@{
        Samples=$samples.Count
        UniqueFrameTimestamps=$unique
        ExplorerFocused=($null -ne $explorerWindow)
        InactiveBeforeDrag=$inactiveBeforeDrag
        DragDeltaX=$dragDeltaX
        DragDeltaY=$dragDeltaY
        MovedOnFirstPress=$movedOnFirstPress
        LeftMouseDownEvents=$leftDown
        LeftMouseUpEvents=$leftUp
        ProcessExited=$process.HasExited
        Passed=($unique -ge 5 -and $inactiveBeforeDrag -and $movedOnFirstPress -and
            $leftDown -ge 1 -and $leftUp -ge 1 -and -not $process.HasExited)
    }
    $result | ConvertTo-Json | Set-Content (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
