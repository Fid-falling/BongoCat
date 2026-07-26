param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [ValidateSet("default", "taskbar", "menu")]
    [string]$Mode = "default"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\window-style-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoStyleNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr handle, int index);
}
'@

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "window-style-audit-$PID"
Start-Sleep -Milliseconds 350
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
$arguments = @("--ci-smoke", "--ci-exit-ms=5000", "--data-root=$data")
if ($Mode -eq "taskbar") { $arguments += "--ci-taskbar-visible" }
if ($Mode -eq "menu") { $arguments += "--ci-menu" }
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -WindowStyle Normal -PassThru
Start-Sleep -Milliseconds 1600
$windows = [Collections.Generic.List[object]]::new()
[BongoCatNeoStyleNative]::EnumWindows({
    param($handle, $data)
    [uint32]$owner = 0
    [void][BongoCatNeoStyleNative]::GetWindowThreadProcessId($handle, [ref]$owner)
    if ($owner -eq $process.Id -and [BongoCatNeoStyleNative]::IsWindowVisible($handle)) {
        $rect = [BongoCatNeoStyleNative+Rect]::new()
        if ([BongoCatNeoStyleNative]::GetWindowRect($handle, [ref]$rect)) {
            $windows.Add([pscustomobject]@{
                Handle=$handle; Area=($rect.R-$rect.L)*($rect.B-$rect.T)
            })
        }
    }
    return $true
}, [IntPtr]::Zero) | Out-Null
$window = $windows | Sort-Object Area -Descending | Select-Object -First 1
$windowStyle = [BongoCatNeoStyleNative]::GetWindowLongPtr($window.Handle, -16).ToInt64()
$style = [BongoCatNeoStyleNative]::GetWindowLongPtr($window.Handle, -20).ToInt64()
if ($Mode -eq "menu") {
    for ($index = 0; $index -lt 40 -and -not $process.HasExited; $index++) {
        if (($style -band 0x20) -ne 0 -and ($style -band 0x8) -ne 0) { break }
        Start-Sleep -Milliseconds 100
        $style = [BongoCatNeoStyleNative]::GetWindowLongPtr($window.Handle, -20).ToInt64()
    }
}
$caption = ($windowStyle -band 0xC00000) -ne 0
$thickFrame = ($windowStyle -band 0x40000) -ne 0
$systemMenu = ($windowStyle -band 0x80000) -ne 0
$captionButtons = ($windowStyle -band 0x30000) -ne 0
$toolWindow = ($style -band 0x80) -ne 0
$appWindow = ($style -band 0x40000) -ne 0
$transparent = ($style -band 0x20) -ne 0
$topmost = ($style -band 0x8) -ne 0
$process.WaitForExit()
$passed = $process.ExitCode -eq 0 -and -not $caption -and -not $thickFrame -and
    -not $systemMenu -and -not $captionButtons -and (($Mode -eq "default" -and
    $toolWindow -and -not $appWindow) -or ($Mode -eq "taskbar" -and
    $appWindow -and -not $toolWindow) -or ($Mode -eq "menu" -and
    $transparent -and $topmost))
[pscustomobject]@{
    Mode=$Mode; Handle=$window.Handle; WindowStyle=("0x{0:X}" -f $windowStyle)
    ExtendedStyle=("0x{0:X}" -f $style); Caption=$caption; ThickFrame=$thickFrame
    SystemMenu=$systemMenu; CaptionButtons=$captionButtons
    ToolWindow=$toolWindow; AppWindow=$appWindow; ClickThrough=$transparent
    Topmost=$topmost; ExitCode=$process.ExitCode; Passed=$passed
} | Format-List
if (-not $passed) { exit 1 }
