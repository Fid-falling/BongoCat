param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\screen-policy" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatScreenPolicyNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatScreenPolicyNative]::SetProcessDPIAware()
$utf8 = [Text.UTF8Encoding]::new($false)
$screens = @([Windows.Forms.Screen]::AllScreens)
$edge = $screens | Sort-Object { $_.WorkingArea.Left } | Select-Object -First 1
$work = $edge.WorkingArea
$minLeft = ($screens.Bounds.Left | Measure-Object -Minimum).Minimum
$minTop = ($screens.Bounds.Top | Measure-Object -Minimum).Minimum

function Write-State([string]$Data, [bool]$Keep, [bool]$WritePreferences,
    [int]$X, [int]$Y) {
    New-Item -ItemType Directory -Force -Path $Data | Out-Null
    if ($WritePreferences) {
        $preferences = @{ format="bongo-cat/preferences"; version=2;
            window=@{ keepInScreen=$Keep } } | ConvertTo-Json -Compress
        [IO.File]::WriteAllText((Join-Path $Data "preferences.json"),
            $preferences, $utf8)
    }
    $session = @{ format="bongo-cat/session"; version=2;
        window=@{ visible=$true; x=$X; y=$Y; width=320; height=240;
            scale=100; opacity=100 }; currentModel="standard" } |
        ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $Data "session.json"), $session, $utf8)
}

function Wait-Window([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $found = [Collections.Generic.List[IntPtr]]::new()
        [BongoCatScreenPolicyNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatScreenPolicyNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatScreenPolicyNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatScreenPolicyNative]::IsWindowVisible($handle) -and
                [BongoCatScreenPolicyNative]::GetWindowRect($handle, [ref]$rect) -and
                $rect.R -gt $rect.L -and $rect.B -gt $rect.T) { $found.Add($handle) }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($found.Count) { return $found[0] }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "BongoCat window was not created"
}

function Get-Rect([IntPtr]$Window) {
    $rect = [BongoCatScreenPolicyNative+Rect]::new()
    if (-not [BongoCatScreenPolicyNative]::GetWindowRect($Window, [ref]$rect)) {
        throw "BongoCat bounds were unavailable"
    }
    return $rect
}

function Start-Case([string]$Name, [bool]$Keep, [bool]$WritePreferences,
    [int]$X, [int]$Y) {
    $data = Join-Path $OutputDir $Name
    Write-State $data $Keep $WritePreferences $X $Y
    $env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
    $env:BONGO_CAT_TEST_INSTANCE_ID = "screen-policy-$Name-$PID"
    $arguments = @("--ci-exit-ms=12000", "--ci-ignore-global-input",
        "--data-root=$data")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    $window = Wait-Window $process.Id
    return [pscustomobject]@{ Process=$process; Window=$window; Data=$data }
}

function Stop-Case($Case) {
    if ($Case.Process -and -not $Case.Process.HasExited) {
        $Case.Process.Kill(); $Case.Process.WaitForExit()
    }
}

function Intersects-Screen($Rect) {
    foreach ($screen in $screens) {
        $bounds = $screen.Bounds
        if ($Rect.L -lt $bounds.Right -and $Rect.R -gt $bounds.Left -and
            $Rect.T -lt $bounds.Bottom -and $Rect.B -gt $bounds.Top) { return $true }
    }
    return $false
}

function Wait-PreferencesFile([string]$Data) {
    $path = Join-Path $Data "preferences.json"
    $deadline = [DateTime]::UtcNow.AddSeconds(4)
    while (-not (Test-Path $path) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    if (-not (Test-Path $path)) { throw "Preferences were not saved" }
    return $path
}

function Invoke-ConstrainedDrag([IntPtr]$Window, [double]$FractionX,
    [double]$FractionY) {
    $before = Get-Rect $Window
    $startX = [int]($before.L + ($before.R - $before.L) * $FractionX)
    $startY = [int]($before.T + ($before.B - $before.T) * $FractionY)
    $pressed = $false
    try {
        [void][BongoCatScreenPolicyNative]::SetCursorPos($startX, $startY)
        Start-Sleep -Milliseconds 120
        [BongoCatScreenPolicyNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
        $pressed = $true
        foreach ($step in 1..6) {
            [void][BongoCatScreenPolicyNative]::SetCursorPos(
                $startX + 10 * $step, $startY)
            Start-Sleep -Milliseconds 35
        }
        $moved = Get-Rect $Window
        $didMove = [Math]::Abs($moved.L - $before.L) -ge 20
        if (-not $didMove) {
            return [pscustomobject]@{ Moved=$false; BlockedWhileHeld=$false }
        }
        foreach ($step in 1..10) {
            $x = [int](($startX + 60) + ($work.Left - ($startX + 60)) * $step / 10)
            [void][BongoCatScreenPolicyNative]::SetCursorPos($x, $startY)
            Start-Sleep -Milliseconds 35
        }
        $held = Get-Rect $Window
        return [pscustomobject]@{
            Moved = $true
            BlockedWhileHeld = $held.L -ge $work.Left
        }
    } finally {
        if ($pressed) {
            [BongoCatScreenPolicyNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
        }
        Start-Sleep -Milliseconds 80
    }
}

function Test-LiveConstrainedDrag([IntPtr]$Window) {
    $original = [BongoCatScreenPolicyNative+Point]::new()
    [void][BongoCatScreenPolicyNative]::GetCursorPos([ref]$original)
    $points = @(
        [pscustomobject]@{ X=.50; Y=.50 },
        [pscustomobject]@{ X=.50; Y=.35 },
        [pscustomobject]@{ X=.35; Y=.50 },
        [pscustomobject]@{ X=.65; Y=.50 },
        [pscustomobject]@{ X=.50; Y=.65 },
        [pscustomobject]@{ X=.35; Y=.35 },
        [pscustomobject]@{ X=.65; Y=.35 })
    try {
        foreach ($point in $points) {
            $result = Invoke-ConstrainedDrag $Window $point.X $point.Y
            if ($result.Moved) { return $result }
        }
        return [pscustomobject]@{ Moved=$false; BlockedWhileHeld=$false }
    } finally {
        [void][BongoCatScreenPolicyNative]::SetCursorPos($original.X, $original.Y)
        Start-Sleep -Milliseconds 120
    }
}

$off = $null; $on = $null; $recovery = $null
try {
    $off = Start-Case "default-off" $false $false ($work.Left + 100) ($work.Top + 100)
    [void][BongoCatScreenPolicyNative]::SetWindowPos($off.Window,
        [IntPtr]::Zero, $work.Left - 100, $work.Top + 100, 320, 240, 0x0014)
    Start-Sleep -Milliseconds 900
    $offRect = Get-Rect $off.Window
    $offAllowed = $offRect.L -lt $work.Left -and $offRect.R -gt $work.Left
    $saved = Get-Content (Wait-PreferencesFile $off.Data) -Raw |
        ConvertFrom-Json
    $defaultOff = $saved.window.keepInScreen -eq $false
    Stop-Case $off; $off = $null

    $on = Start-Case "enabled" $true $true ($work.Left + 100) ($work.Top + 100)
    [void][BongoCatScreenPolicyNative]::SetWindowPos($on.Window,
        [IntPtr]::Zero, $work.Left - 100, $work.Top + 100, 320, 240, 0x0014)
    Start-Sleep -Milliseconds 900
    $onRect = Get-Rect $on.Window
    $onConstrained = $onRect.L -ge $work.Left -and $onRect.T -ge $work.Top
    $liveDrag = Test-LiveConstrainedDrag $on.Window
    Stop-Case $on; $on = $null

    $recovery = Start-Case "detached-display" $false $false `
        ($minLeft - 2500) ($minTop - 2500)
    Start-Sleep -Milliseconds 500
    $recoveryRect = Get-Rect $recovery.Window
    $detachedRecovered = Intersects-Screen $recoveryRect
    $passed = $defaultOff -and $offAllowed -and $onConstrained -and
        $liveDrag.Moved -and $liveDrag.BlockedWhileHeld -and $detachedRecovered
    $result = [ordered]@{ DefaultOff=$defaultOff; OffAllowsPartial=$offAllowed;
        EnabledConstrained=$onConstrained; EnabledLiveDragMoved=$liveDrag.Moved;
        EnabledBlockedWhileHeld=$liveDrag.BlockedWhileHeld;
        DetachedRecovered=$detachedRecovered; Passed=$passed }
    $result | ConvertTo-Json | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    $result | Format-List
    if (-not $passed) { exit 1 }
} finally {
    if ($off) { Stop-Case $off }
    if ($on) { Stop-Case $on }
    if ($recovery) { Stop-Case $recovery }
}
