param([string]$Exe = "", [string]$OutputDir = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\preferences-robustness" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
$configPath = Join-Path $data "preferences.json"
$framePath = Join-Path $data "preferences-window.txt"
New-Item -ItemType Directory -Force -Path $data | Out-Null
$initial = '{"format":"bongo-cat/preferences","version":2,"app":{"language":"zh-CN","theme":"light"},"shortcuts":{"visibleCat":"Control+Shift+B"}}'
[IO.File]::WriteAllText($configPath, $initial, [Text.UTF8Encoding]::new($false))

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatRobustNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hgt, uint f);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect r);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr r);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
[void][BongoCatRobustNative]::SetProcessDPIAware()
Add-Type -AssemblyName System.Drawing

function Wait-Window {
    $path = Join-Path $data "preferences-window.txt"
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'handle=(\d+)') { return [IntPtr][long]$Matches[1] }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Wait-StartupReady {
    $path = Join-Path $data "startup.log"
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'Startup ready') { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "BongoCat startup did not become ready"
}

function Point-At([IntPtr]$Window, [double]$X, [double]$Y) {
    $p = [BongoCatRobustNative+Point]::new()
    $p.X = [int][Math]::Round($X * $script:dpiScale)
    $p.Y = [int][Math]::Round($Y * $script:dpiScale)
    [void][BongoCatRobustNative]::ClientToScreen($Window, [ref]$p)
    return $p
}

function Click-At([IntPtr]$Window, [double]$X, [double]$Y) {
    $p = Point-At $Window $X $Y
    [void][BongoCatRobustNative]::SetForegroundWindow($Window)
    [void][BongoCatRobustNative]::SetCursorPos($p.X, $p.Y)
    $client = [BongoCatRobustNative+Point]::new()
    $client.X = [int][Math]::Round($X * $script:dpiScale)
    $client.Y = [int][Math]::Round($Y * $script:dpiScale)
    $packed = [IntPtr](($client.X -band 0xffff) -bor (($client.Y -band 0xffff) -shl 16))
    $clip = [BongoCatRobustNative+Rect]::new()
    $clip.L = $p.X; $clip.T = $p.Y; $clip.R = $p.X + 1; $clip.B = $p.Y + 1
    [void][BongoCatRobustNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatRobustNative]::SendMessageW($Window, 0x0200, [IntPtr]::Zero, $packed)
        Start-Sleep -Milliseconds 80
        [void][BongoCatRobustNative]::SendMessageW($Window, 0x0201, [IntPtr]1, $packed)
        Start-Sleep -Milliseconds 50
        [void][BongoCatRobustNative]::SetCursorPos($p.X, $p.Y)
        [void][BongoCatRobustNative]::SendMessageW($Window, 0x0202, [IntPtr]::Zero, $packed)
    } finally { [void][BongoCatRobustNative]::ReleaseCursor([IntPtr]::Zero) }
    Start-Sleep -Milliseconds 300
}

function Move-At([IntPtr]$Window, [double]$X, [double]$Y) {
    $p = Point-At $Window $X $Y
    [void][BongoCatRobustNative]::SetForegroundWindow($Window)
    [void][BongoCatRobustNative]::SetCursorPos($p.X, $p.Y)
    $clientX = [int][Math]::Round($X * $script:dpiScale)
    $clientY = [int][Math]::Round($Y * $script:dpiScale)
    $packed = [IntPtr](($clientX -band 0xffff) -bor (($clientY -band 0xffff) -shl 16))
    [void][BongoCatRobustNative]::SendMessageW($Window, 0x0200, [IntPtr]::Zero, $packed)
    Start-Sleep -Milliseconds 350
}

function Set-LogicalSize([IntPtr]$Window, [int]$Width, [int]$Height) {
    $targetWidth = [int][Math]::Round($Width * $script:dpiScale)
    $targetHeight = [int][Math]::Round($Height * $script:dpiScale)
    [void][BongoCatRobustNative]::SetWindowPos($Window, [IntPtr](-1), 0, 0,
        $targetWidth, $targetHeight, 0x0042)
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        $client = [BongoCatRobustNative+Rect]::new()
        [void][BongoCatRobustNative]::GetClientRect($Window, [ref]$client)
        if ([Math]::Abs($client.R - $targetWidth) -le 2 -and
            [Math]::Abs($client.B - $targetHeight) -le 2) { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences resize did not reach ${Width}x${Height}"
}

function Wheel-Down([IntPtr]$Window, [double]$X, [double]$Y) {
    $p = Point-At $Window $X $Y
    [void][BongoCatRobustNative]::SetForegroundWindow($Window)
    [void][BongoCatRobustNative]::SetCursorPos($p.X, $p.Y)
    Start-Sleep -Milliseconds 80
    for ($i = 0; $i -lt 4; ++$i) {
        [BongoCatRobustNative]::mouse_event(0x0800, 0, 0, [uint32]4294967176,
            [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
    }
    Start-Sleep -Milliseconds 250
}

function Save-Window([IntPtr]$Window, [string]$Name) {
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        $rect = [BongoCatRobustNative+Rect]::new()
        $valid = [BongoCatRobustNative]::GetClientRect($Window, [ref]$rect) -and
            $rect.R -gt 0 -and $rect.B -gt 0
        if ($valid) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $valid) { throw "Preferences client area was unavailable for $Name" }
    $bitmap = [Drawing.Bitmap]::new($rect.R, $rect.B)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { $printed = [BongoCatRobustNative]::PrintWindow($Window, $dc, 2) }
    finally { $graphics.ReleaseHdc($dc) }
    if (-not $printed) { throw "PrintWindow failed for $Name" }
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Wait-Config([scriptblock]$Condition = $null) {
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        if (Test-Path -LiteralPath $configPath) {
            try {
                $config = Get-Content -Raw -Encoding UTF8 $configPath | ConvertFrom-Json
                if (-not $Condition -or (& $Condition $config)) { return $config }
            }
            catch {}
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Page-Is([int]$Expected) {
    $text = Get-Content -Raw -LiteralPath $framePath -ErrorAction SilentlyContinue
    return $text -match "page=$Expected( |$)"
}

function Wait-Shortcut-Smoke {
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $text = Get-Content -Raw -LiteralPath $framePath -ErrorAction SilentlyContinue
        if ($text -match 'recording=0' -and $text -match 'shortcut_smoke=0') {
            return $true
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-robustness-$PID"
$arguments = @("--ci-preferences", "--ci-preference-page=1",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-input-audit",
    "--ci-preference-shortcut", "--ci-exit-ms=25000", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Window
    Wait-StartupReady
    $client = [BongoCatRobustNative+Rect]::new()
    [void][BongoCatRobustNative]::GetClientRect($window, [ref]$client)
    $script:dpiScale = ($client.R - $client.L) / 900.0
    Set-LogicalSize $window 720 560
    Start-Sleep -Milliseconds 500
    Click-At $window 600 352
    [void](Save-Window $window "small-combo-open.png")
    Wheel-Down $window 600 500
    [void](Save-Window $window "small-combo-scrolled.png")
    Click-At $window 600 541
    $small = Wait-Config { param($config) $config.app.language -eq "vi-VN" }
    $languageScrollable = $null -ne $small -and $small.app.language -eq "vi-VN"

    Set-LogicalSize $window 900 680
    Start-Sleep -Milliseconds 500
    Click-At $window 82 342
    Click-At $window 468 320
    [void](Save-Window $window "behavior-modal.png")
    Move-At $window 684 178
    [void](Save-Window $window "behavior-modal-close-hover.png")
    Click-At $window 82 418
    $behaviorModalBlocksSidebar = Page-Is 2
    [BongoCatRobustNative]::keybd_event(0x1b, 0, 0, [UIntPtr]::Zero)
    [BongoCatRobustNative]::keybd_event(0x1b, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    Click-At $window 82 418
    Click-At $window 756 215
    $shortcutAttemptFinished = Wait-Shortcut-Smoke
    $shortcuts = Wait-Config { param($config)
        $config.shortcuts.visibleCat -eq "Control+Shift+B" -and
        $config.shortcuts.visiblePreference -ne "Control+Shift+B" }
    $duplicateRejected = $shortcutAttemptFinished -and $null -ne $shortcuts -and
        $shortcuts.shortcuts.visibleCat -eq "Control+Shift+B" -and
        $shortcuts.shortcuts.visiblePreference -ne "Control+Shift+B"
    Move-At $window 822 141
    [void](Save-Window $window "shortcut-clear-hover.png")
    $result = [ordered]@{
        LanguageScrollable=$languageScrollable
        BehaviorModalBlocksSidebar=$behaviorModalBlocksSidebar
        DuplicateShortcutRejected=$duplicateRejected
        Passed=$languageScrollable -and $behaviorModalBlocksSidebar -and $duplicateRejected
    }
    $result | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
