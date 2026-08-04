param([string]$Exe = "", [string]$OutputDir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\behavior-management" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
$configPath = Join-Path $data "preferences.json"
New-Item -ItemType Directory -Force -Path $data | Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "behavior-management-audit-$PID"
$env:BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN = "1"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatBehaviorAuditNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct Rect { public int L, T, R, B; }
    public struct Point { public int X, Y; }
}
'@
[void][BongoCatBehaviorAuditNative]::SetProcessDPIAware()

function Wait-Window([Diagnostics.Process]$Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $found = [Collections.Generic.List[object]]::new()
        [BongoCatBehaviorAuditNative]::EnumWindows({ param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatBehaviorAuditNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            if ($owner -eq $Process.Id -and
                [BongoCatBehaviorAuditNative]::IsWindowVisible($handle)) {
                $r = [BongoCatBehaviorAuditNative+Rect]::new()
                if ([BongoCatBehaviorAuditNative]::GetClientRect($handle, [ref]$r))
                    { $found.Add([pscustomobject]@{Handle=$handle; W=$r.R; H=$r.B}) }
            }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        $window = $found | Where-Object W -gt 700 | Sort-Object W -Descending |
            Select-Object -First 1
        if ($window) { return $window }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for preferences window"
}

function Click-At([object]$Window, [double]$X, [double]$Y) {
    $scale = $Window.W / 900.0
    $clientX = [int][Math]::Round($X * $scale)
    $clientY = [int][Math]::Round($Y * $scale)
    $point = [BongoCatBehaviorAuditNative+Point]::new()
    $point.X = $clientX; $point.Y = $clientY
    [void][BongoCatBehaviorAuditNative]::ClientToScreen($Window.Handle, [ref]$point)
    [void][BongoCatBehaviorAuditNative]::SetForegroundWindow($Window.Handle)
    [void][BongoCatBehaviorAuditNative]::SetCursorPos($point.X, $point.Y)
    $packed = [IntPtr](($clientX -band 0xffff) -bor (($clientY -band 0xffff) -shl 16))
    [void][BongoCatBehaviorAuditNative]::SendMessageW(
        $Window.Handle, 0x0200, [IntPtr]::Zero, $packed)
    [void][BongoCatBehaviorAuditNative]::SendMessageW(
        $Window.Handle, 0x0201, [IntPtr]1, $packed)
    Start-Sleep -Milliseconds 60
    [void][BongoCatBehaviorAuditNative]::SendMessageW(
        $Window.Handle, 0x0202, [IntPtr]::Zero, $packed)
    Start-Sleep -Milliseconds 400
}

function Save-Window([object]$Window, [string]$Name) {
    $bitmap = [Drawing.Bitmap]::new($Window.W, $Window.H)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { $ok = [BongoCatBehaviorAuditNative]::PrintWindow(
        $Window.Handle, $dc, 2) } finally { $graphics.ReleaseHdc($dc) }
    if (-not $ok) { throw "PrintWindow failed" }
    $bitmap.Save((Join-Path $OutputDir $Name), [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}

function Press-Key([byte]$Key) {
    [BongoCatBehaviorAuditNative]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [BongoCatBehaviorAuditNative]::keybd_event($Key, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}

function Paste-Text([string]$Text) {
    [Windows.Forms.Clipboard]::SetText($Text)
    [BongoCatBehaviorAuditNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    Press-Key 0x56
    [BongoCatBehaviorAuditNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function Start-Preferences {
    $arguments = @("--ci-preferences", "--ci-preference-page=2",
        "--ci-language=zh-CN", "--ci-theme=light", "--ci-exit-ms=30000",
        "--preferences=$configPath", "--data-root=$data")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    $window = Wait-Window $process
    $scale = $window.W / 900.0
    [void][BongoCatBehaviorAuditNative]::SetWindowPos($window.Handle,
        [IntPtr](-1), 0, 0, [int](900 * $scale), [int](680 * $scale), 0x0042)
    Start-Sleep -Milliseconds 500
    $window.H = [int](680 * $scale)
    return [pscustomobject]@{Process=$process; Window=$window}
}

function Stop-Preferences([object]$Run) {
    if (-not $Run.Process.HasExited) { Stop-Process -Id $Run.Process.Id -Force }
    [void]$Run.Process.WaitForExit(3000)
}

function Wait-Label {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        if (Test-Path $configPath) {
            try {
                $config = Get-Content -Raw -Encoding UTF8 $configPath | ConvertFrom-Json
                $match = @($config.behaviorShortcuts | Where-Object {
                    $_.id -eq "standard:motion:CAT_motion:0" -and
                    $_.label -eq "Renamed Motion" })
                if ($match.Count -eq 1) { return $config }
            } catch {}
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Renamed behavior was not persisted"
}

function Model-Hashes {
    $modelRoot = Join-Path $root "resources\assets\models"
    $result = @{}
    Get-ChildItem $modelRoot -Recurse -File -Filter "*.json" | ForEach-Object {
        $result[$_.FullName.Substring($modelRoot.Length)] =
            (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    }
    return $result
}

function Hashes-Match([hashtable]$Before, [hashtable]$After) {
    if ($Before.Count -ne $After.Count) { return $false }
    foreach ($key in $Before.Keys) {
        if (-not $After.ContainsKey($key) -or $Before[$key] -ne $After[$key])
            { return $false }
    }
    return $true
}

$clipboardHadText = [Windows.Forms.Clipboard]::ContainsText()
$clipboardText = if ($clipboardHadText) { [Windows.Forms.Clipboard]::GetText() } else { "" }
try {
  $beforeHashes = Model-Hashes
  $folderOpened = $false
  $currentModelPreserved = $false
  $run = Start-Preferences
  try {
    Click-At $run.Window 468 320
    Save-Window $run.Window "behavior-open.png"
    Click-At $run.Window 260 318
    Save-Window $run.Window "behavior-editing.png"
    [void][BongoCatBehaviorAuditNative]::SetForegroundWindow($run.Window.Handle)
    Paste-Text "Renamed Motion"
    Press-Key 0x0d
    Start-Sleep -Milliseconds 500
    Save-Window $run.Window "behavior-after-input.png"
    $saved = Wait-Label
    Save-Window $run.Window "behavior-renamed.png"
    Press-Key 0x1b
    Start-Sleep -Milliseconds 500
    $shell = New-Object -ComObject Shell.Application
    $beforeWindows = @($shell.Windows() | ForEach-Object { $_.HWND })
    Click-At $run.Window 800 320
    $keyboard = Get-ChildItem $data -Recurse -Directory | Where-Object {
        $_.FullName -like "*\assets\models\keyboard" } | Select-Object -First 1
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        foreach ($item in @($shell.Windows())) {
            try {
                if ($keyboard -and $item.Document.Folder.Self.Path -eq $keyboard.FullName) {
                    $folderOpened = $true
                    if ($beforeWindows -notcontains $item.HWND) { $item.Quit() }
                }
            } catch {}
        }
        if (-not $folderOpened) { Start-Sleep -Milliseconds 100 }
    } while (-not $folderOpened -and [DateTime]::UtcNow -lt $deadline)
    Start-Sleep -Milliseconds 500
    $sessionPath = Join-Path $data "session.json"
    if (Test-Path $sessionPath) {
        $session = Get-Content -Raw -Encoding UTF8 $sessionPath | ConvertFrom-Json
        $currentModelPreserved = $session.currentModel -eq "standard"
    }
  } finally { Stop-Preferences $run }

  $reloaded = Start-Preferences
  try {
    Click-At $reloaded.Window 468 320
    Save-Window $reloaded.Window "behavior-renamed-reloaded.png"
    $persistedAfterRestart = $null -ne (Wait-Label)
  } finally { Stop-Preferences $reloaded }
  $sourceFilesUnchanged = Hashes-Match $beforeHashes (Model-Hashes)
  $passed = $folderOpened -and $currentModelPreserved -and
      $persistedAfterRestart -and $sourceFilesUnchanged
  [pscustomobject]@{Passed=$passed; FolderOpened=$folderOpened;
      CurrentModelPreserved=$currentModelPreserved;
      PersistedAfterRestart=$persistedAfterRestart;
      SourceFilesUnchanged=$sourceFilesUnchanged} |
      ConvertTo-Json | Tee-Object (Join-Path $OutputDir "report.json")
} finally {
    if ($clipboardHadText) { [Windows.Forms.Clipboard]::SetText($clipboardText) }
    else { [Windows.Forms.Clipboard]::Clear() }
}
if (-not $passed) { exit 1 }
