param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [string]$Session = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\model-selection" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-$PID-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
if ($Session) {
    Copy-Item -LiteralPath ([IO.Path]::GetFullPath($Session)) `
        -Destination (Join-Path $data "session.json")
}
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatModelSelectionNative {
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
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle,
        uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatModelSelectionNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $found = [Collections.Generic.List[IntPtr]]::new()
        [BongoCatModelSelectionNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatModelSelectionNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatModelSelectionNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatModelSelectionNative]::IsWindowVisible($handle) -and
                [BongoCatModelSelectionNative]::GetClientRect($handle, [ref]$rect) -and
                $rect.R -ge 700 -and $rect.B -ge 550) { $found.Add($handle) }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($found.Count) { return $found[0] }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Get-ClientPoint([IntPtr]$Window, [double]$X, [double]$Y) {
    $client = [BongoCatModelSelectionNative+Rect]::new()
    [void][BongoCatModelSelectionNative]::GetClientRect($Window, [ref]$client)
    $point = [BongoCatModelSelectionNative+Point]::new()
    $clientX = [int][Math]::Round($X * ($client.R - $client.L) / 900.0)
    $clientY = [int][Math]::Round($Y * ($client.B - $client.T) / 680.0)
    $point.X = $clientX; $point.Y = $clientY
    [void][BongoCatModelSelectionNative]::ClientToScreen($Window, [ref]$point)
    return [pscustomobject]@{ X=$point.X; Y=$point.Y; ClientX=$clientX; ClientY=$clientY }
}

function Invoke-PhysicalClick([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatModelSelectionNative]::SetForegroundWindow($Window)
    [void][BongoCatModelSelectionNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 120
    $clip = [BongoCatModelSelectionNative+Rect]::new()
    $clip.L = $point.X; $clip.T = $point.Y
    $clip.R = $point.X + 1; $clip.B = $point.Y + 1
    $packed = [IntPtr](($point.ClientX -band 0xffff) -bor
        (($point.ClientY -band 0xffff) -shl 16))
    [void][BongoCatModelSelectionNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatModelSelectionNative]::SendMessageW(
            $Window, 0x0200, [IntPtr]::Zero, $packed)
        [void][BongoCatModelSelectionNative]::SendMessageW(
            $Window, 0x0201, [IntPtr]1, $packed)
        Start-Sleep -Milliseconds 50
        [void][BongoCatModelSelectionNative]::SetCursorPos($point.X, $point.Y)
        [void][BongoCatModelSelectionNative]::SendMessageW(
            $Window, 0x0202, [IntPtr]::Zero, $packed)
    } finally {
        [void][BongoCatModelSelectionNative]::ReleaseCursor([IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 350
}

function Save-Client([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatModelSelectionNative+Rect]::new()
    [void][BongoCatModelSelectionNative]::GetClientRect($Window, [ref]$rect)
    $origin = [BongoCatModelSelectionNative+Point]::new()
    [void][BongoCatModelSelectionNative]::ClientToScreen($Window, [ref]$origin)
    $bitmap = [Drawing.Bitmap]::new($rect.R, $rect.B)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
    $bitmap.Save((Join-Path $OutputDir $Name), [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}

function Get-FrameCount {
    $path = Join-Path $data "frame-series.csv"
    if (-not (Test-Path $path)) { return 0 }
    return @(Import-Csv $path).Count
}

function Measure-NewFrames([int]$Before) {
    Start-Sleep -Milliseconds 500
    $rows = @(Import-Csv (Join-Path $data "frame-series.csv"))
    $fresh = @($rows | Select-Object -Skip $Before)
    return [pscustomobject]@{
        Count = $fresh.Count
        MinimumVisiblePixels = ($fresh | Measure-Object visible_pixels -Minimum).Minimum
        MinimumAlphaPixels = ($fresh | Measure-Object alpha_pixels -Minimum).Minimum
        Modes = @($fresh.model_mode | Select-Object -Unique)
        MaximumSerial = ($fresh | Measure-Object selection_serial -Maximum).Maximum
        InvalidFrames = @($fresh | Where-Object {
            [int]$_.visible_pixels -eq 0 -or [int]$_.alpha_pixels -eq 0 -or
            [int]$_.model_state_consistent -ne 1 -or
            [int]$_.window_config_visible -ne 1 -or
            [int]$_.window_os_visible -ne 1 }).Count
    }
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "model-selection-audit-$PID"
$arguments = @("--ci-smoke", "--ci-preferences", "--ci-preference-page=2",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-frame-series",
    "--ci-freeze-model", "--ci-input-audit", "--ci-ignore-global-input",
    "--ci-exit-ms=18000", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatModelSelectionNative]::ShowWindow($window, 9)
    [void][BongoCatModelSelectionNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 0, 0, 0x0041)
    [void][BongoCatModelSelectionNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 900
    Save-Client $window "before.png"
    $before = Get-FrameCount
    Invoke-PhysicalClick $window 750 210
    Save-Client $window "after-keyboard.png"
    $keyboardFrames = Measure-NewFrames $before
    if (Test-Path (Join-Path $data "frame.bmp")) {
        Copy-Item (Join-Path $data "frame.bmp") (Join-Path $OutputDir "keyboard-main.bmp")
    }
    $keyboard = $keyboardFrames.Modes -contains "keyboard"
    $before = Get-FrameCount
    Invoke-PhysicalClick $window 290 450
    Save-Client $window "after-gamepad.png"
    $gamepadFrames = Measure-NewFrames $before
    if (Test-Path (Join-Path $data "frame.bmp")) {
        Copy-Item (Join-Path $data "frame.bmp") (Join-Path $OutputDir "gamepad-main.bmp")
    }
    $gamepad = $gamepadFrames.Modes -contains "gamepad"
    $before = Get-FrameCount
    Invoke-PhysicalClick $window 290 450
    $repeatFrames = Measure-NewFrames $before
    $cache = Get-ChildItem -LiteralPath $data -Directory |
        Where-Object Name -like "embedded-assets-*" | Select-Object -First 1
    if (-not $cache) { throw "Embedded asset cache was not created" }
    $texture = Join-Path $cache.FullName `
        "assets\models\keyboard\demomodel2.1024\texture_00.png"
    if (-not (Test-Path $texture)) { throw "Keyboard failure fixture was not found" }
    Remove-Item -LiteralPath $texture -Force
    $before = Get-FrameCount
    Invoke-PhysicalClick $window 750 210
    Save-Client $window "after-failed-keyboard.png"
    $failureFrames = Measure-NewFrames $before
    if (Test-Path (Join-Path $data "frame.bmp")) {
        Copy-Item (Join-Path $data "frame.bmp") `
            (Join-Path $OutputDir "failed-switch-main.bmp")
    }
    $passed = $keyboard -and $gamepad -and
        $keyboardFrames.Count -gt 0 -and $keyboardFrames.InvalidFrames -eq 0 -and
        $gamepadFrames.Count -gt 0 -and $gamepadFrames.InvalidFrames -eq 0 -and
        $repeatFrames.Count -gt 0 -and $repeatFrames.InvalidFrames -eq 0 -and
        $repeatFrames.Modes -contains "gamepad" -and
        [int]$repeatFrames.MaximumSerial -gt [int]$gamepadFrames.MaximumSerial -and
        $failureFrames.Count -gt 0 -and $failureFrames.InvalidFrames -eq 0 -and
        $failureFrames.Modes -contains "gamepad" -and
        [int]$failureFrames.MaximumSerial -eq [int]$repeatFrames.MaximumSerial
    $result = [ordered]@{ KeyboardSelected=$keyboard; KeyboardFrames=$keyboardFrames
        GamepadSelected=$gamepad; GamepadFrames=$gamepadFrames
        RepeatFrames=$repeatFrames; FailedSwitchRecovery=$failureFrames; Passed=$passed }
    $result | ConvertTo-Json -Depth 3 | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    $result | ConvertTo-Json -Depth 3
    if (-not $passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill(); $process.WaitForExit()
    }
}
