param(
    [int]$ProcessId,
    [IntPtr]$Source,
    [int]$Left,
    [int]$Top,
    [int]$Right,
    [int]$Bottom,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BongoCatDynamicClickNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr handle, StringBuilder text, int count);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr handle, int index);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
}
'@

function Visible-Proxy {
    $matches = [Collections.Generic.List[IntPtr]]::new()
    [BongoCatDynamicClickNative]::EnumWindows({
        param($handle, $unused)
        [uint32]$owner = 0
        [void][BongoCatDynamicClickNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId -and [BongoCatDynamicClickNative]::IsWindowVisible($handle)) {
            $class = [Text.StringBuilder]::new(64)
            [void][BongoCatDynamicClickNative]::GetClassName($handle, $class, 64)
            if ($class.ToString() -eq "BongoCat.LayeredPresenter") { $matches.Add($handle) }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $matches.Count -gt 0
}

function Event-Count([string]$Path) {
    try { return @(Get-Content -LiteralPath $Path -ErrorAction Stop).Count }
    catch { return 0 }
}

function Click-Left {
    [BongoCatDynamicClickNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    [BongoCatDynamicClickNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}

$events = Join-Path $OutputDir "dynamic-click-events.txt"
$ready = Join-Path $OutputDir "dynamic-click-ready.txt"
Remove-Item -LiteralPath $events, $ready -Force -ErrorAction SilentlyContinue
$targetScript = Join-Path $PSScriptRoot "ClickThroughTarget.ps1"
$target = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $targetScript,
    "-Events", $events, "-Ready", $ready, "-X", ($Left - 20), "-Y", ($Top - 20),
    "-Width", ($Right - $Left + 40), "-Height", ($Bottom - $Top + 40))
$transparent = $false
$opaque = $false
$proxyVisible = $false
$samples = [Collections.Generic.List[object]]::new()
try {
    for ($i = 0; $i -lt 50 -and -not (Test-Path $ready); ++$i) {
        Start-Sleep -Milliseconds 50
    }
    if (Test-Path $ready) {
        # Keep the pet immediately above the target in the topmost band so transparent
        # hit tests cannot fall through to an unrelated desktop window.
        [void][BongoCatDynamicClickNative]::SetWindowPos(
            $Source, [IntPtr](-1), 0, 0, 0, 0, 0x13)
        Start-Sleep -Milliseconds 100
        $points = @(@(($Left + 3), ($Top + 3)), @(($Right - 4), ($Top + 3)),
            @(($Left + 3), ($Bottom - 4)), @(($Right - 4), ($Bottom - 4)))
        foreach ($point in $points) {
            [void][BongoCatDynamicClickNative]::SetCursorPos($point[0], $point[1])
            Start-Sleep -Milliseconds 180
            $proxyVisible = $proxyVisible -or (Visible-Proxy)
            $nativePoint = [BongoCatDynamicClickNative+Point]::new()
            $nativePoint.X = $point[0]; $nativePoint.Y = $point[1]
            $hit = [BongoCatDynamicClickNative]::WindowFromPoint($nativePoint)
            [uint32]$hitProcess = 0
            [void][BongoCatDynamicClickNative]::GetWindowThreadProcessId($hit, [ref]$hitProcess)
            $hitClass = [Text.StringBuilder]::new(64)
            [void][BongoCatDynamicClickNative]::GetClassName($hit, $hitClass, 64)
            $before = Event-Count $events
            Click-Left
            Start-Sleep -Milliseconds 120
            $after = Event-Count $events
            $samples.Add([pscustomobject]@{ X=$point[0]; Y=$point[1]
                SourceStyle=("0x{0:X}" -f [BongoCatDynamicClickNative]::GetWindowLongPtr($Source, -20).ToInt64())
                Hit=("0x{0:X}" -f $hit.ToInt64()); HitProcess=$hitProcess
                HitClass=$hitClass.ToString(); EventsBefore=$before; EventsAfter=$after })
            if ($after -ge $before + 2) { $transparent = $true; break }
        }
        $centerX = [int](($Left + $Right) / 2)
        $centerY = [int](($Top + $Bottom) / 2)
        [void][BongoCatDynamicClickNative]::SetCursorPos($centerX, $centerY)
        Start-Sleep -Milliseconds 180
        $proxyVisible = $proxyVisible -or (Visible-Proxy)
        $before = Event-Count $events
        Click-Left
        Start-Sleep -Milliseconds 120
        $opaque = (Event-Count $events) -eq $before
    }
} finally {
    if ($target -and -not $target.HasExited) { $target.Kill(); $target.WaitForExit() }
}

[pscustomobject]@{
    Passed = $transparent -and $opaque -and -not $proxyVisible
    TransparentClickPassed = $transparent
    OpaqueClickBlocked = $opaque
    DynamicProxyVisible = $proxyVisible
    Samples = $samples
}
