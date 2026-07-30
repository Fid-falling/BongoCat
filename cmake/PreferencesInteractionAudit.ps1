param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\preferences-interaction" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$dataRoot = Join-Path $OutputDir "data"
$configPath = Join-Path $dataRoot "preferences.json"
$sessionPath = Join-Path $dataRoot "session.json"
New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $sessionPath -Force -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot "VisualAuditHelpers.ps1")
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatPreferencesNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatPreferencesNative]::SetProcessDPIAware()

function Get-AppWindows([int]$ProcessId) {
    $windows = [Collections.Generic.List[object]]::new()
    [BongoCatPreferencesNative]::EnumWindows({
        param($handle, $unused)
        [uint32]$owner = 0
        [void][BongoCatPreferencesNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId -and [BongoCatPreferencesNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatPreferencesNative+Rect]::new()
            if ([BongoCatPreferencesNative]::GetWindowRect($handle, [ref]$rect)) {
                    $width = $rect.R - $rect.L; $height = $rect.B - $rect.T
                    $area = $width * $height
                    if ($area -gt 400) { $windows.Add([pscustomobject]@{
                        Handle=$handle; Area=$area; Width=$width; Height=$height }) }
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
        $preferences = @($windows | Where-Object {
            $_.Width -ge 700 -and $_.Height -ge 550 })
        if ($preferences.Count) {
            return ($preferences | Sort-Object Area -Descending |
                Select-Object -First 1).Handle
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Get-ClientPoint([IntPtr]$Window, [double]$X, [double]$Y) {
    $client = [BongoCatPreferencesNative+Rect]::new()
    [void][BongoCatPreferencesNative]::GetClientRect($Window, [ref]$client)
    $point = [BongoCatPreferencesNative+Point]::new()
    $clientX = [int][Math]::Round($X * ($client.R - $client.L) / 900.0)
    $clientY = [int][Math]::Round($Y * ($client.B - $client.T) / 680.0)
    $point.X = $clientX; $point.Y = $clientY
    [void][BongoCatPreferencesNative]::ClientToScreen($Window, [ref]$point)
    return [pscustomobject]@{ X=$point.X; Y=$point.Y; ClientX=$clientX; ClientY=$clientY }
}

function Focus-Window([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatPreferencesNative]::SetForegroundWindow($Window)
    [void][BongoCatPreferencesNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 300
}

function Invoke-PhysicalClick([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatPreferencesNative]::SetForegroundWindow($Window)
    [void][BongoCatPreferencesNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 80
    $clip = [BongoCatPreferencesNative+Rect]::new()
    $clip.L = $point.X; $clip.T = $point.Y
    $clip.R = $point.X + 1; $clip.B = $point.Y + 1
    $packed = [IntPtr](($point.ClientX -band 0xffff) -bor
        (($point.ClientY -band 0xffff) -shl 16))
    [void][BongoCatPreferencesNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatPreferencesNative]::SendMessageW(
            $Window, 0x0200, [IntPtr]::Zero, $packed)
        [void][BongoCatPreferencesNative]::SendMessageW(
            $Window, 0x0201, [IntPtr]1, $packed)
        Start-Sleep -Milliseconds 50
        [void][BongoCatPreferencesNative]::SetCursorPos($point.X, $point.Y)
        [void][BongoCatPreferencesNative]::SendMessageW(
            $Window, 0x0202, [IntPtr]::Zero, $packed)
    } finally { [void][BongoCatPreferencesNative]::ReleaseCursor([IntPtr]::Zero) }
    Start-Sleep -Milliseconds 300
}

function Invoke-Wheel([IntPtr]$Window, [double]$X, [double]$Y) {
    $point = Get-ClientPoint $Window $X $Y
    [void][BongoCatPreferencesNative]::SetForegroundWindow($Window)
    [void][BongoCatPreferencesNative]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 50
    $position = [IntPtr]([long](($point.Y -band 0xFFFF) -shl 16) -bor
        [long]($point.X -band 0xFFFF))
    $wheel = [IntPtr]([long][uint32]4287102976)
    [void][BongoCatPreferencesNative]::PostMessageW($Window, 0x020A, $wheel, $position)
    Start-Sleep -Milliseconds 80
    [void][BongoCatPreferencesNative]::PostMessageW($Window, 0x020A, $wheel, $position)
    Start-Sleep -Milliseconds 300
}

function Save-Window([IntPtr]$Window, [string]$Name) {
    [void][BongoCatPreferencesNative]::ShowWindow($Window, 9)
    [void][BongoCatPreferencesNative]::SetWindowPos(
        $Window, [IntPtr](-1), 20, 20, 0, 0, 0x0041)
    [void][BongoCatPreferencesNative]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 180
    $rect = [BongoCatPreferencesNative+Rect]::new()
    [void][BongoCatPreferencesNative]::GetWindowRect($Window, [ref]$rect)
    $bitmap = [Drawing.Bitmap]::new($rect.R - $rect.L, $rect.B - $rect.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $printed = $false
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $dc = $graphics.GetHdc()
        try { $printed = [BongoCatPreferencesNative]::PrintWindow($Window, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        $capture = Measure-Capture $bitmap
        if ($printed -and $capture.Colors.Count -ge 16 -and
            $capture.BlackRatio -lt 0.02) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $printed -or $capture.Colors.Count -lt 16 -or
        $capture.BlackRatio -ge 0.02) {
        $graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
    }
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Measure-Difference([string]$First, [string]$Second) {
    $a = [Drawing.Bitmap]::new($First); $b = [Drawing.Bitmap]::new($Second)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) { return 1.0 }
        $changed = 0; $samples = 0
        for ($y = 0; $y -lt $a.Height; $y += 6) {
            for ($x = 0; $x -lt $a.Width; $x += 6) {
                if ($a.GetPixel($x, $y).ToArgb() -ne $b.GetPixel($x, $y).ToArgb()) { $changed++ }
                $samples++
            }
        }
        return $changed / [double]$samples
    } finally { $a.Dispose(); $b.Dispose() }
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-interaction-audit-$PID"
$arguments = @("--ci-preferences", "--ci-preference-page=0", "--ci-language=zh-CN",
    "--ci-theme=light", "--ci-input-audit", "--ci-preference-shortcut",
    "--preferences=$configPath", "--data-root=$dataRoot")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    Focus-Window $window 450 620
    $baseline = Save-Window $window "01-baseline.png"
    Invoke-PhysicalClick $window 820 141
    $toggled = Save-Window $window "02-toggle-card.png"
    Invoke-PhysicalClick $window 833 293
    Invoke-PhysicalClick $window 833 371
    $debouncePersisted = $false
    for ($i = 0; $i -lt 40 -and -not $debouncePersisted; $i++) {
        Start-Sleep -Milliseconds 50
        if (Test-Path $configPath) {
            try {
                $liveConfig = Get-Content -Raw -Encoding utf8 $configPath | ConvertFrom-Json
                $debouncePersisted = $liveConfig.window.passThrough -eq $true
            } catch {}
        }
    }
    Invoke-Wheel $window 450 540
    Focus-Window $window 450 520
    [BongoCatPreferencesNative]::keybd_event(0x22, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [BongoCatPreferencesNative]::keybd_event(0x22, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $scrolled = Save-Window $window "03-scrolled.png"
    Invoke-Wheel $window 450 540
    [void](Save-Window $window "03b-controls.png")

    Invoke-PhysicalClick $window 82 266
    $general = Save-Window $window "04-general.png"
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        Invoke-PhysicalClick $window 765 353
        $combo = Save-Window $window "05-combo-open.png"
        Invoke-PhysicalClick $window 765 435
        Start-Sleep -Milliseconds 500
        try {
            $live = Get-Content -Raw -Encoding utf8 $configPath | ConvertFrom-Json
            if ($live.app.language -eq "zh-TW") { break }
        } catch {}
    }
    $language = Save-Window $window "05c-language-live.png"
    Invoke-PhysicalClick $window 82 418
    $shortcuts = Save-Window $window "06-shortcuts.png"
    Invoke-PhysicalClick $window 756 141
    Start-Sleep -Milliseconds 500
    $edited = Save-Window $window "07-shortcut-edited.png"

    Invoke-PhysicalClick $window 82 495
    $about = Save-Window $window "08-about.png"
    [void][BongoCatPreferencesNative]::PostMessageW($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    for ($i = 0; $i -lt 50 -and -not (Test-Path $configPath); $i++) {
        Start-Sleep -Milliseconds 50
    }
    $config = Get-Content -Raw -Encoding utf8 $configPath | ConvertFrom-Json
    $session = Get-Content -Raw -Encoding utf8 $sessionPath | ConvertFrom-Json
    $result = [ordered]@{
        ToggleDifference = Measure-Difference $baseline $toggled
        ScrollDifference = Measure-Difference $toggled $scrolled
        PageDifference = Measure-Difference $scrolled $general
        ComboDifference = Measure-Difference $general $combo
        LanguageDifference = Measure-Difference $general $language
        EditDifference = Measure-Difference $shortcuts $edited
        DebouncePersisted = $debouncePersisted
        PreferencesFormatValid = $config.format -eq "bongo-cat/preferences" -and
            $config.version -eq 2
        SessionFormatValid = $session.format -eq "bongo-cat/session" -and
            $session.version -eq 2
        LanguagePersisted = $config.app.language -eq "zh-TW"
        TogglePersisted = $config.window.passThrough -eq $true
        ScreenPolicyPersisted = $config.window.keepInScreen -eq $true
        StepperPersisted = $session.window.scale -eq 101
        ShortcutPersisted = $config.shortcuts.visibleCat -eq "Control+Shift+B"
    }
    $result | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    $passed = $result.ToggleDifference -gt 0.0001 -and $result.ScrollDifference -gt 0.02 -and
        $result.PageDifference -gt 0.02 -and $result.ComboDifference -gt 0.002 -and
        $result.EditDifference -gt 0.0001 -and $result.DebouncePersisted -and
        $result.PreferencesFormatValid -and $result.SessionFormatValid -and
        $result.LanguagePersisted -and $result.TogglePersisted -and
        $result.ScreenPolicyPersisted -and
        $result.StepperPersisted -and
        $result.ShortcutPersisted
    if (-not $passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
