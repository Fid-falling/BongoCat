param(
    [string]$Exe = "",
    [string]$ModelDirectory = "",
    [string]$OutputDir = "",
    [switch]$NativePrompt
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build\BongoCatNeo.exe" }
if (-not $ModelDirectory) {
    $ModelDirectory = Join-Path $root "resources\assets\models\standard"
}
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\drop-import-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$ModelDirectory = [IO.Path]::GetFullPath($ModelDirectory)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BongoCatNeoDropNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr handle, StringBuilder name, int count);
    [DllImport("shell32.dll")] public static extern void DragAcceptFiles(IntPtr handle, bool accept);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("kernel32.dll")] public static extern IntPtr GlobalAlloc(uint flags, UIntPtr bytes);
    [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr memory);
    [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr memory);
    public static string ClassName(IntPtr handle) {
        StringBuilder name = new StringBuilder(128);
        GetClassNameW(handle, name, name.Capacity);
        return name.ToString();
    }
    public static bool Drop(IntPtr window, string path) {
        byte[] text = Encoding.Unicode.GetBytes(path + "\0\0");
        IntPtr memory = GlobalAlloc(0x42, (UIntPtr)(20 + text.Length));
        if (memory == IntPtr.Zero) return false;
        IntPtr data = GlobalLock(memory);
        Marshal.WriteInt32(data, 0, 20);
        Marshal.WriteInt32(data, 16, 1);
        Marshal.Copy(text, 0, IntPtr.Add(data, 20), text.Length);
        GlobalUnlock(memory);
        DragAcceptFiles(window, true);
        return PostMessageW(window, 0x233, memory, IntPtr.Zero);
    }
}
'@

function Save-Window([IntPtr]$Handle, [string]$Path) {
    $rect = [BongoCatNeoDropNative+Rect]::new()
    if (-not [BongoCatNeoDropNative]::GetWindowRect($Handle, [ref]$rect)) { return $null }
    [void][BongoCatNeoDropNative]::ShowWindow($Handle, 9)
    [void][BongoCatNeoDropNative]::SetWindowPos(
        $Handle, [IntPtr](-1), 0, 0, 0, 0, 0x43)
    [void][BongoCatNeoDropNative]::SetForegroundWindow($Handle)
    Start-Sleep -Milliseconds 500
    $bitmap = [Drawing.Bitmap]::new($rect.R-$rect.L, $rect.B-$rect.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
    $graphics.Dispose()
    $colors = [Collections.Generic.HashSet[int]]::new()
    $dark = 0
    for ($y=0; $y -lt $bitmap.Height; $y+=5) { for ($x=0; $x -lt $bitmap.Width; $x+=5) {
        $pixel = $bitmap.GetPixel($x,$y)
        [void]$colors.Add(($pixel.R -shl 16) -bor ($pixel.G -shl 8) -bor $pixel.B)
        if ($pixel.R + $pixel.G + $pixel.B -lt 690) { $dark++ }
    }}
    $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    [void][BongoCatNeoDropNative]::SetWindowPos(
        $Handle, [IntPtr](-2), 0, 0, 0, 0, 0x03)
    return [pscustomobject]@{ Colors=$colors.Count; DarkPixels=$dark }
}

function Get-AppWindows([int]$Id) {
    $items = [Collections.Generic.List[object]]::new()
    [BongoCatNeoDropNative]::EnumWindows({
        param($handle, $data)
        [uint32]$owner = 0
        [void][BongoCatNeoDropNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $Id -and [BongoCatNeoDropNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatNeoDropNative+Rect]::new()
            if ([BongoCatNeoDropNative]::GetWindowRect($handle, [ref]$rect)) {
                $items.Add([pscustomobject]@{
                    Handle=$handle; Area=($rect.R-$rect.L)*($rect.B-$rect.T)
                    Class=[BongoCatNeoDropNative]::ClassName($handle)
                })
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $items
}

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "drop-import-audit-$PID"
Start-Sleep -Milliseconds $(if ($NativePrompt) { 1500 } else { 350 })
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$dataRoot = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
$arguments = @("--ci-preferences", "--ci-preference-page=2", "--data-root=$dataRoot")
if (-not $NativePrompt) { $arguments = @("--ci-smoke", "--ci-exit-ms=6000") + $arguments }
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -WindowStyle Normal -PassThru
$windowDeadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $windows = @(Get-AppWindows $process.Id)
    if ($windows.Count -lt 2) { Start-Sleep -Milliseconds 100 }
} until ($windows.Count -ge 2 -or [DateTime]::UtcNow -ge $windowDeadline)
$target = ($windows | Sort-Object Area -Descending | Select-Object -First 1).Handle
$posted = $target -and [BongoCatNeoDropNative]::Drop($target, $ModelDirectory)
$deadline = [DateTime]::UtcNow.AddSeconds(5)
do {
    Start-Sleep -Milliseconds 100
    $pendingModels = @(Get-ChildItem (Join-Path $dataRoot "custom-models") `
        -Directory -ErrorAction SilentlyContinue)
} until ($pendingModels.Count -eq 1 -or [DateTime]::UtcNow -ge $deadline)
$promptClosed = $true
if ($NativePrompt) {
    $sawDialog = $false
    $dialogDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $dialogs = @(Get-AppWindows $process.Id | Where-Object Class -eq "#32770")
        if ($dialogs.Count) {
            $sawDialog = $true
            foreach ($dialog in $dialogs) {
                [void][BongoCatNeoDropNative]::PostMessageW(
                    $dialog.Handle, 0x111, [IntPtr]1, [IntPtr]::Zero)
            }
        }
        Start-Sleep -Milliseconds 100
    } until ($sawDialog -and -not @(Get-AppWindows $process.Id |
        Where-Object Class -eq "#32770").Count -or
        [DateTime]::UtcNow -ge $dialogDeadline)
    $promptClosed = $sawDialog -and -not @(Get-AppWindows $process.Id |
        Where-Object Class -eq "#32770").Count
}
Start-Sleep -Milliseconds 350
$framePath = Join-Path $OutputDir "after-import.png"
$frame = if ($target) { Save-Window $target $framePath } else { $null }
if ($NativePrompt) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    $process.WaitForExit()
    $exitCode = 0
} else { $process.WaitForExit(); $exitCode = $process.ExitCode }
$models = @(Get-ChildItem (Join-Path $dataRoot "custom-models") `
    -Directory -ErrorAction SilentlyContinue)
$settings = @($models | ForEach-Object {
    Get-ChildItem $_.FullName -Filter "*.model3.json" -File -ErrorAction SilentlyContinue
})
$passed = $posted -and $promptClosed -and $exitCode -eq 0 -and $models.Count -ge 1 -and
    $settings.Count -eq $models.Count -and $frame -and $frame.Colors -ge 16 -and
    $frame.DarkPixels -ge 400
[pscustomobject]@{
    Posted=$posted; Target=$target; ImportedCount=$models.Count
    ImportedModel3=$settings.Count; ExitCode=$exitCode; PromptClosed=$promptClosed
    FrameColors=if($frame){$frame.Colors}else{0}
    DarkPixels=if($frame){$frame.DarkPixels}else{0}
    Passed=$passed; DataRoot=$dataRoot; Frame=$framePath
} | Format-List
if (-not $passed) { exit 1 }
