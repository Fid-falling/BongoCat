param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [string]$ImportPath = "",
    [string]$Language = "zh-CN",
    [ValidateSet("light", "dark")]
    [string]$Theme = "light"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\cursor-audit" }
if (-not $ImportPath) { $ImportPath = Join-Path $root "build-cubism\import-fixture-plain" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$ImportPath = [IO.Path]::GetFullPath($ImportPath)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoCursorNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] public struct CursorInfo {
        public int Size, Flags; public IntPtr Cursor; public Point Screen;
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr handle, int index);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CursorInfo info);
    [DllImport("user32.dll")] public static extern IntPtr LoadCursor(IntPtr instance, IntPtr name);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, int data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatNeoCursorNative]::SetProcessDPIAware()

function Get-AppWindows([int]$ProcessId) {
    $windows = [Collections.Generic.List[object]]::new()
    [BongoCatNeoCursorNative]::EnumWindows({
        param($handle, $unused)
        [uint32]$owner = 0
        [void][BongoCatNeoCursorNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId -and [BongoCatNeoCursorNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatNeoCursorNative+Rect]::new()
            if ([BongoCatNeoCursorNative]::GetWindowRect($handle, [ref]$rect)) {
                $area = ($rect.R - $rect.L) * ($rect.B - $rect.T)
                $width = $rect.R - $rect.L; $height = $rect.B - $rect.T
                if ($area -gt 400 -and $width -ge 700 -and $height -ge 550) {
                    $windows.Add([pscustomobject]@{
                        Handle=$handle; Area=$area; Width=$width; Height=$height })
                }
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $windows
}

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $windows = @(Get-AppWindows $ProcessId)
        if ($windows.Count -ge 1) {
            return ($windows | Sort-Object Area -Descending | Select-Object -First 1).Handle
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Get-ClientPoint([IntPtr]$Window, [double]$X, [double]$Y) {
    $client = [BongoCatNeoCursorNative+Rect]::new()
    [void][BongoCatNeoCursorNative]::GetClientRect($Window, [ref]$client)
    $point = [BongoCatNeoCursorNative+Point]::new()
    $point.X = [int][Math]::Round($X * ($client.R - $client.L) / 900.0)
    $point.Y = [int][Math]::Round($Y * ($client.B - $client.T) / 680.0)
    [void][BongoCatNeoCursorNative]::ClientToScreen($Window, [ref]$point)
    return $point
}

function Invoke-Click([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatNeoCursorNative]::SetForegroundWindow($Window)
    [void][BongoCatNeoCursorNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 250
    [BongoCatNeoCursorNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [BongoCatNeoCursorNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

function Invoke-Wheel([IntPtr]$Window, [double]$X, [double]$Y, [int]$Delta) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatNeoCursorNative]::SetForegroundWindow($Window)
    [void][BongoCatNeoCursorNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 120
    [BongoCatNeoCursorNative]::mouse_event(0x0800, 0, 0, $Delta, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 450
}

function Test-Cursor([IntPtr]$Window, [string]$Name, [double]$X,
    [double]$Y, [int]$SystemId) {
    $point = Get-ClientPoint $Window $X $Y
    $client = [BongoCatNeoCursorNative+Rect]::new()
    [void][BongoCatNeoCursorNative]::GetClientRect($Window, [ref]$client)
    $clientX = [int][Math]::Round($X * ($client.R - $client.L) / 900.0)
    $clientY = [int][Math]::Round($Y * ($client.B - $client.T) / 680.0)
    $position = [IntPtr]([long](($clientY -band 0xFFFF) -shl 16) -bor
        [long]($clientX -band 0xFFFF))
    $expected = [BongoCatNeoCursorNative]::LoadCursor([IntPtr]::Zero, [IntPtr]$SystemId)
    $info = $null
    for ($attempt = 0; $attempt -lt 4; $attempt++) {
        [void][BongoCatNeoCursorNative]::SetForegroundWindow($Window)
        [void][BongoCatNeoCursorNative]::SetCursorPos($point.X +
            $(if ($attempt % 2) { 1 } else { -1 }), $point.Y)
        Start-Sleep -Milliseconds 45
        [void][BongoCatNeoCursorNative]::SetCursorPos($point.X, $point.Y)
        [void][BongoCatNeoCursorNative]::PostMessageW(
            $Window, 0x0200, [IntPtr]::Zero, $position)
        Start-Sleep -Milliseconds 250
        $info = [BongoCatNeoCursorNative+CursorInfo]::new()
        $info.Size = [Runtime.InteropServices.Marshal]::SizeOf($info)
        [void][BongoCatNeoCursorNative]::GetCursorInfo([ref]$info)
        if ($info.Cursor -eq $expected) { break }
    }
    $arrow = [BongoCatNeoCursorNative]::LoadCursor([IntPtr]::Zero, [IntPtr]32512)
    $text = [BongoCatNeoCursorNative]::LoadCursor([IntPtr]::Zero, [IntPtr]32513)
    $hand = [BongoCatNeoCursorNative]::LoadCursor([IntPtr]::Zero, [IntPtr]32649)
    $actual = if ($info.Cursor -eq $hand) { "hand" } elseif ($info.Cursor -eq $text) {
        "text" } elseif ($info.Cursor -eq $arrow) { "arrow" } else { "other" }
    return [pscustomobject]@{ Name=$Name; Actual=$actual; Passed=$info.Cursor -eq $expected }
}

function Save-Window([IntPtr]$Window, [string]$Name) {
    [void][BongoCatNeoCursorNative]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 300
    $rect = [BongoCatNeoCursorNative+Rect]::new()
    [void][BongoCatNeoCursorNative]::GetWindowRect($Window, [ref]$rect)
    $bitmap = [Drawing.Bitmap]::new($rect.R - $rect.L, $rect.B - $rect.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}

function Start-AuditPage([int]$Page, [bool]$Import) {
    # Move away from transient selection/translation overlays before opening the
    # next window; otherwise a topmost helper can intercept the first hover.
    [void][BongoCatNeoCursorNative]::SetCursorPos(2, 2)
    Start-Sleep -Milliseconds 400
    $dataRoot = Join-Path $OutputDir "data-page$Page"
    Remove-Item -LiteralPath $dataRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
    $arguments = @("--ci-preferences", "--ci-preference-page=$Page",
        "--ci-language=$Language", "--ci-theme=$Theme", "--data-root=$dataRoot")
    if ($Import) {
        $arguments += "`"--ci-import=$ImportPath`""
    }
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    $window = Wait-Preferences $process.Id
    [void][BongoCatNeoCursorNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 800
    return [pscustomobject]@{ Process=$process; Window=$window }
}

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "cursor-audit-$PID"
$results = [Collections.Generic.List[object]]::new()
$hand = 32649; $text = 32513; $arrow = 32512

$page = Start-AuditPage 0 $false
try {
    Save-Window $page.Window "display-page.png"
    $results.Add((Test-Cursor $page.Window "tab" 82 190 $hand))
    $results.Add((Test-Cursor $page.Window "close" 861 36 $hand))
    $results.Add((Test-Cursor $page.Window "toggle" 820 141 $hand))
    $results.Add((Test-Cursor $page.Window "toggle-secondary" 820 596 $hand))
    $results.Add((Test-Cursor $page.Window "empty-space" 500 640 $arrow))
} finally { $page.Process.Kill(); $page.Process.WaitForExit() }

$page = Start-AuditPage 1 $false
try {
    $results.Add((Test-Cursor $page.Window "combo" 765 353 $hand))
} finally { $page.Process.Kill(); $page.Process.WaitForExit() }

$page = Start-AuditPage 3 $false
try {
    $results.Add((Test-Cursor $page.Window "shortcut-field" 756 141 $hand))
} finally { $page.Process.Kill(); $page.Process.WaitForExit() }

$page = Start-AuditPage 2 $true
try {
    $results.Add((Test-Cursor $page.Window "import-card" 291 230 $hand))
    $results.Add((Test-Cursor $page.Window "model-card" 523 230 $hand))
    $results.Add((Test-Cursor $page.Window "model-close" 578 559 $hand))
    Save-Window $page.Window "model-page.png"
    Invoke-Click $page.Window 578 559
    Save-Window $page.Window "remove-dialog.png"
    $results.Add((Test-Cursor $page.Window "cancel-button" 523 376 $hand))
    Invoke-Click $page.Window 523 376
    Save-Window $page.Window "after-cancel.png"
} finally { $page.Process.Kill(); $page.Process.WaitForExit() }

$results | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDir "result.json")
$results | Format-Table
if (@($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
