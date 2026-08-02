param([string]$Exe = "", [string]$OutputDir = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-delivery-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-delivery-final\click-through-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BongoCatClickNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr handle, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr handle, StringBuilder text, int count);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr handle, int index);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
}
'@
[void][BongoCatClickNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
Add-Type -AssemblyName System.Drawing

function Save-Shot($Window, [string]$Path) {
    $bitmap = [Drawing.Bitmap]::new($Window.R - $Window.L, $Window.B - $Window.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try { $graphics.CopyFromScreen($Window.L, $Window.T, 0, 0, $bitmap.Size); $bitmap.Save($Path) }
    catch { }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
}

function Get-AppWindows([int]$ProcessId) {
    $windows = [Collections.Generic.List[object]]::new()
    [BongoCatClickNative]::EnumWindows({
        param($handle, $unused)
        [uint32]$owner = 0
        [void][BongoCatClickNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $class = [Text.StringBuilder]::new(256)
            [void][BongoCatClickNative]::GetWindowText($handle, $title, 256)
            [void][BongoCatClickNative]::GetClassName($handle, $class, 256)
            $rect = [BongoCatClickNative+Rect]::new()
            [void][BongoCatClickNative]::GetWindowRect($handle, [ref]$rect)
            $windows.Add([pscustomobject]@{ Handle=$handle; Title=$title.ToString()
                Class=$class.ToString(); Visible=[BongoCatClickNative]::IsWindowVisible($handle)
                Iconic=[BongoCatClickNative]::IsIconic($handle); L=$rect.L; T=$rect.T
                R=$rect.R; B=$rect.B
                ExStyle=[BongoCatClickNative]::GetWindowLongPtr($handle, -20).ToInt64() })
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return @($windows)
}

function Wait-Until([scriptblock]$Condition, [int]$Milliseconds = 3000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($Milliseconds)
    do {
        $value = & $Condition
        if ($value) { return $value }
        Start-Sleep -Milliseconds 20
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Frame-Count([string]$Path) {
    try { return @(Get-Content -LiteralPath $Path -ErrorAction Stop).Count - 1 }
    catch { return 0 }
}

function Wait-LowFpsBoundary([string]$Path) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $count = 0
    do {
        try { $rows = @(Import-Csv -LiteralPath $Path -ErrorAction Stop) }
        catch { $rows = @() }
        if ($rows.Count -gt $count) {
            $count = $rows.Count
            if ($count -ge 2 -and
                [int64]$rows[-1].ticks_ns - [int64]$rows[-2].ticks_ns -gt 700000000) {
                return $count
            }
        }
        Start-Sleep -Milliseconds 15
    } while ([DateTime]::UtcNow -lt $deadline)
    return 0
}

function Send-Key([byte]$Key) {
    [BongoCatClickNative]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 10
    [BongoCatClickNative]::keybd_event($Key, 0, 2, [UIntPtr]::Zero)
}

function Same-Rect($Left, $Right) {
    return $Left -and $Right -and $Left.L -eq $Right.L -and $Left.T -eq $Right.T -and
        $Left.R -eq $Right.R -and $Left.B -eq $Right.B
}

$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
$preferences = @{ format="bongo-cat/preferences"; version=2
    model=@{ maxFPS=1; ignoreMouse=$false }
    window=@{ passThrough=$false; alwaysOnTop=$true; taskbarVisible=$false; keepInScreen=$true }
    app=@{ trayVisible=$false }
    shortcuts=@{ visibleCat="F23"; penetrable="F24" } } | ConvertTo-Json -Depth 5 -Compress
$session = @{ format="bongo-cat/session"; version=2; currentModel="keyboard"
    window=@{ visible=$true; scale=60.0; opacity=99.0; x=240; y=180; width=384; height=216 } } |
    ConvertTo-Json -Depth 5 -Compress
[IO.File]::WriteAllText((Join-Path $data "preferences.json"), $preferences,
    [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $data "session.json"), $session,
    [Text.UTF8Encoding]::new($false))

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "click-audit-$PID"
$arguments = @("--ci-smoke", "--ci-frame-series", "--ci-input-audit",
    "--ci-exit-ms=30000", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
$cursor = [BongoCatClickNative+Point]::new()
[void][BongoCatClickNative]::GetCursorPos([ref]$cursor)
$failures = [Collections.Generic.List[string]]::new()
$snapshots = [Collections.Generic.List[object]]::new()
$framePath = Join-Path $data "frame-series.csv"
try {
    $source = Wait-Until {
        Get-AppWindows $process.Id | Where-Object {
            $_.Class -eq "SDL_app" -and $_.Title -like "BongoCat - Pet*" } |
            Select-Object -First 1
    } 10000
    if (-not $source) { throw "Pet source window was not created" }
    if (-not (Wait-Until { (Frame-Count $framePath) -ge 1 } 10000)) {
        throw "The first pet frame was not rendered"
    }
    [void][BongoCatClickNative]::SetCursorPos(20, 20)
    if (-not (Wait-LowFpsBoundary $framePath)) {
        throw "Could not synchronize with the 1 FPS frame boundary"
    }

    $timer = [Diagnostics.Stopwatch]::StartNew()
    Send-Key 0x87 # F24: forced click-through
    $proxy = Wait-Until {
        Get-AppWindows $process.Id | Where-Object {
            $_.Class -eq "BongoCat.LayeredPresenter" -and $_.Visible } |
            Select-Object -First 1
    } 1000
    $timer.Stop()
    $source = Get-AppWindows $process.Id | Where-Object Class -eq "SDL_app" |
        Where-Object Title -like "BongoCat - Pet*" | Select-Object -First 1
    if (-not $proxy) { $failures.Add("Proxy did not appear after enabling click-through") }
    if ($timer.ElapsedMilliseconds -gt 350) {
        $failures.Add("Click-through redraw took $($timer.ElapsedMilliseconds) ms at 1 FPS")
    }
    if (($source.ExStyle -band 0x28) -ne 0x28) {
        $failures.Add("Source did not become topmost and click-through")
    }
    if ($proxy -and ($proxy.ExStyle -band 0x800A8) -ne 0x800A8) {
        $failures.Add("Proxy did not inherit layered, click-through, tool and topmost styles")
    }
    if (-not (Same-Rect $source $proxy)) { $failures.Add("Proxy bounds differ from source") }
    $center = [BongoCatClickNative+Point]::new()
    $center.X = [int](($source.L + $source.R) / 2)
    $center.Y = [int](($source.T + $source.B) / 2)
    $hit = [BongoCatClickNative]::WindowFromPoint($center)
    if ($hit -eq $source.Handle -or ($proxy -and $hit -eq $proxy.Handle)) {
        $failures.Add("WindowFromPoint still hit BongoCat while forced click-through was enabled")
    }
    $snapshots.Add([pscustomobject]@{ Stage="enabled"; DelayMs=$timer.ElapsedMilliseconds
        SourceStyle=("0x{0:X}" -f $source.ExStyle); ProxyStyle=("0x{0:X}" -f $proxy.ExStyle) })

    $process.Refresh()
    $gdiBefore = [BongoCatClickNative]::GetGuiResources($process.Handle, 0)
    for ($index = 0; $index -lt 36; ++$index) {
        $width = 420 + ($index % 7) * 18
        $height = [int][Math]::Round($width * 0.5625)
        [void][BongoCatClickNative]::SetWindowPos($source.Handle, [IntPtr]::Zero,
            $source.L, $source.T, $width, $height, 0x14)
        Start-Sleep -Milliseconds 12
    }
    $source = Wait-Until { Get-AppWindows $process.Id | Where-Object Class -eq "SDL_app" |
        Where-Object Title -like "BongoCat - Pet*" | Select-Object -First 1 } 1000
    $proxy = Wait-Until { $all = Get-AppWindows $process.Id
        $s = $all | Where-Object Class -eq "SDL_app" | Where-Object Title -like "BongoCat - Pet*" |
            Select-Object -First 1
        $p = $all | Where-Object Class -eq "BongoCat.LayeredPresenter" | Select-Object -First 1
        if (Same-Rect $s $p) { $p } } 1500
    $process.Refresh()
    $gdiAfter = [BongoCatClickNative]::GetGuiResources($process.Handle, 0)
    if (-not $proxy) { $failures.Add("Proxy did not follow repeated diagonal resizes") }
    if ([int]$gdiAfter - [int]$gdiBefore -gt 3) {
        $failures.Add("GDI objects grew from $gdiBefore to $gdiAfter during resize")
    }
    $snapshots.Add([pscustomobject]@{ Stage="resize"; GdiBefore=$gdiBefore; GdiAfter=$gdiAfter })

    [void][BongoCatClickNative]::ShowWindow($source.Handle, 6)
    if (-not (Wait-Until { [BongoCatClickNative]::IsIconic($source.Handle) } 1500)) {
        $failures.Add("Source did not minimize")
    }
    $minimizedFrames = Frame-Count $framePath
    Start-Sleep -Milliseconds 1250
    $proxyNow = Get-AppWindows $process.Id | Where-Object Class -eq "BongoCat.LayeredPresenter" |
        Select-Object -First 1
    if ($proxyNow.Visible) { $failures.Add("Proxy remained visible while source was minimized") }
    if ((Frame-Count $framePath) -ne $minimizedFrames) {
        $failures.Add("Pet continued rendering while minimized")
    }
    [void][BongoCatClickNative]::ShowWindow($source.Handle, 9)
    $proxy = Wait-Until { Get-AppWindows $process.Id | Where-Object {
        $_.Class -eq "BongoCat.LayeredPresenter" -and $_.Visible } | Select-Object -First 1 } 2000
    if (-not $proxy) { $failures.Add("Proxy did not return after restore") }

    Send-Key 0x86 # F23: hide
    if (-not (Wait-Until { $all = Get-AppWindows $process.Id
        -not @($all | Where-Object { $_.Class -in @("SDL_app", "BongoCat.LayeredPresenter") -and
            $_.Title -like "BongoCat - Pet*" -and $_.Visible }).Count } 1500)) {
        $failures.Add("Pet windows did not hide together")
    }
    Send-Key 0x86
    $proxy = Wait-Until { Get-AppWindows $process.Id | Where-Object {
        $_.Class -eq "BongoCat.LayeredPresenter" -and $_.Visible } | Select-Object -First 1 } 2000
    if (-not $proxy) { $failures.Add("Proxy did not return after hide/show") }

    Save-Shot $source (Join-Path $OutputDir "active-proxy.png")
    Send-Key 0x87
    $restoredValues = @(Wait-Until { $all = Get-AppWindows $process.Id
        $s = $all | Where-Object Class -eq "SDL_app" | Where-Object Title -like "BongoCat - Pet*" |
            Select-Object -First 1
        $p = $all | Where-Object Class -eq "BongoCat.LayeredPresenter" | Select-Object -First 1
        if ($s.Visible -and -not $p.Visible -and ($s.ExStyle -band 0x20) -eq 0) { $s } } 1500)
    $restored = if ($restoredValues.Count) { $restoredValues[-1] } else { $null }
    if (-not $restored) {
        $failures.Add("Source did not restore after disabling click-through")
    }
    if ($restored) { Save-Shot $restored (Join-Path $OutputDir "restored-source.png") }

    $dynamicProxy = $null
    if ($restored) {
        $points = @(@(($restored.L + 3), ($restored.T + 3)),
            @(($restored.R - 4), ($restored.T + 3)),
            @(($restored.L + 3), ($restored.B - 4)),
            @(($restored.R - 4), ($restored.B - 4)))
        foreach ($point in $points) {
            [void][BongoCatClickNative]::SetCursorPos($point[0], $point[1])
            $dynamicProxy = Wait-Until { Get-AppWindows $process.Id | Where-Object {
                $_.Class -eq "BongoCat.LayeredPresenter" -and $_.Visible } |
                Select-Object -First 1 } 700
            if ($dynamicProxy) { break }
        }
        if (-not $dynamicProxy) {
            $failures.Add("Transparent model corners did not dynamically pass through")
        } else {
            $center.X = [int](($restored.L + $restored.R) / 2)
            $center.Y = [int](($restored.T + $restored.B) / 2)
            [void][BongoCatClickNative]::SetCursorPos($center.X, $center.Y)
            if (-not (Wait-Until { $all = Get-AppWindows $process.Id
                $s = $all | Where-Object Class -eq "SDL_app" |
                    Where-Object Title -like "BongoCat - Pet*" | Select-Object -First 1
                $p = $all | Where-Object Class -eq "BongoCat.LayeredPresenter" |
                    Select-Object -First 1
                if ($s.Visible -and -not $p.Visible -and ($s.ExStyle -band 0x20) -eq 0) {
                    $s } } 1500)) {
                $failures.Add("Opaque model content did not become clickable again")
            }
        }
    }
    $snapshots.Add([pscustomobject]@{ Stage="lifecycle"; MinimizedFrames=$minimizedFrames
        DynamicProxy=($null -ne $dynamicProxy); FinalFrames=(Frame-Count $framePath) })
} finally {
    [void][BongoCatClickNative]::SetCursorPos($cursor.X, $cursor.Y)
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    Remove-Item Env:BONGO_CAT_ALLOW_TEST_INSTANCES -ErrorAction SilentlyContinue
    Remove-Item Env:BONGO_CAT_TEST_INSTANCE_ID -ErrorAction SilentlyContinue
}

$passed = $failures.Count -eq 0
$result = [ordered]@{ Passed=$passed; Failures=$failures; Snapshots=$snapshots; Data=$data }
$result | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $OutputDir "result.json")
$result | ConvertTo-Json -Depth 5
if (-not $passed) { exit 1 }
