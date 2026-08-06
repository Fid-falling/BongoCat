param(
    [Parameter(Mandatory=$true)][int]$MverPid,
    [Parameter(Mandatory=$true)][int]$NativePid,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [int]$WindowSize = 428,
    [switch]$PointerOnly,
    [switch]$IdleOnly,
    [double]$IdleDurationSeconds = 50.0,
    [int]$IdleIntervalMilliseconds = 200
)

$ErrorActionPreference = "Stop"
if ($PointerOnly -and $IdleOnly) {
    throw "PointerOnly and IdleOnly cannot be combined"
}
if ($IdleOnly -and ($IdleDurationSeconds -le 0 -or
    $IdleIntervalMilliseconds -lt 50)) {
    throw "Idle capture requires a positive duration and an interval of at least 50 ms"
}
. (Join-Path $PSScriptRoot "MverParityCaptureSupport.ps1")

$mverOriginal = Get-Rect $mver.MainWindowHandle
$nativeOriginal = Get-Rect $native.MainWindowHandle
$mverWidth = $mverOriginal.Right - $mverOriginal.Left
$mverHeight = $mverOriginal.Bottom - $mverOriginal.Top
$nativeWidth = $nativeOriginal.Right - $nativeOriginal.Left
$nativeHeight = $nativeOriginal.Bottom - $nativeOriginal.Top
$background = [Windows.Forms.Form]::new()
$background.FormBorderStyle = [Windows.Forms.FormBorderStyle]::None
$background.BackColor = [Drawing.Color]::Black
$background.StartPosition = [Windows.Forms.FormStartPosition]::Manual
$background.Bounds = [Windows.Forms.SystemInformation]::VirtualScreen
$background.ShowInTaskbar = $false
$background.TopMost = $false

try {
    $background.Show()
    [Windows.Forms.Application]::DoEvents()
    # A controllable asInvoker reference is required because CopyFromScreen
    # cannot capture pixels that remain beyond the physical desktop edge.
    $mverCapture = Set-CaptureWindowPosition $mver.MainWindowHandle 140 100 `
        $mverWidth $mverHeight "Mver"
    $screen = [Windows.Forms.SystemInformation]::VirtualScreen
    $margin = 32
    $candidates = @(
        [Drawing.Point]::new($screen.Left + $margin,
            $screen.Bottom - $nativeHeight - $margin),
        [Drawing.Point]::new($screen.Right - $nativeWidth - $margin,
            $screen.Bottom - $nativeHeight - $margin),
        [Drawing.Point]::new($screen.Left + $margin, $screen.Top + $margin),
        [Drawing.Point]::new($screen.Right - $nativeWidth - $margin,
            $screen.Top + $margin)
    )
    $nativePosition = $null
    foreach ($candidate in $candidates) {
        $separate = $candidate.X + $nativeWidth + $margin -le $mverCapture.Left -or
            $candidate.X - $margin -ge $mverCapture.Right -or
            $candidate.Y + $nativeHeight + $margin -le $mverCapture.Top -or
            $candidate.Y - $margin -ge $mverCapture.Bottom
        if ($separate) { $nativePosition = $candidate; break }
    }
    if (-not $nativePosition) {
        throw "Cannot place parity windows without overlap on the virtual screen"
    }
    [MverParityInput]::SetWindowPos($background.Handle, [IntPtr](-2),
        0, 0, 0, 0, 0x13) | Out-Null
    Set-CaptureWindowPosition $native.MainWindowHandle $nativePosition.X `
        $nativePosition.Y $nativeWidth $nativeHeight "Native" | Out-Null
    [MverParityInput]::SetForegroundWindow($mver.MainWindowHandle) | Out-Null
    for ($attempt = 1; $attempt -le 8 -and
        [MverParityInput]::GetForegroundWindow() -ne $mver.MainWindowHandle; $attempt++) {
        Start-Sleep -Milliseconds 50
        [MverParityInput]::SetForegroundWindow($mver.MainWindowHandle) | Out-Null
    }
    if ([MverParityInput]::GetForegroundWindow() -ne $mver.MainWindowHandle) {
        throw "Cannot bring the Mver reference above the capture background"
    }
    # Keep Native foreground so input injection reaches both applications.
    [MverParityInput]::SetForegroundWindow($native.MainWindowHandle) | Out-Null
    for ($attempt = 1; $attempt -le 8 -and [MverParityInput]::GetForegroundWindow() -ne $native.MainWindowHandle; $attempt++) {
        Start-Sleep -Milliseconds 50; [MverParityInput]::SetForegroundWindow(
            $native.MainWindowHandle) | Out-Null
    }
    if ([MverParityInput]::GetForegroundWindow() -ne $native.MainWindowHandle) {
        throw "Cannot focus Native for parity input injection" }
    Start-Sleep -Milliseconds 700

    if ($IdleOnly) {
        $screen = [Windows.Forms.Screen]::PrimaryScreen.Bounds
        [Windows.Forms.Cursor]::Position = [Drawing.Point]::new(
            $screen.Left + [int]($screen.Width / 2),
            $screen.Top + [int]($screen.Height / 2))
        Start-Sleep -Milliseconds 1200
        $idleFrames = [int][Math]::Floor(
            $IdleDurationSeconds * 1000.0 / $IdleIntervalMilliseconds) + 1
        $timings = [Collections.Generic.List[object]]::new()
        $clock = [Diagnostics.Stopwatch]::StartNew()
        for ($frame = 0; $frame -lt $idleFrames; $frame++) {
            $target = [int64]$frame * $IdleIntervalMilliseconds
            while ($clock.ElapsedMilliseconds -lt $target) {
                $remaining = $target - $clock.ElapsedMilliseconds
                Start-Sleep -Milliseconds ([Math]::Min(10, [Math]::Max(1, $remaining)))
            }
            $before = $clock.Elapsed.TotalMilliseconds
            Save-Pair ("idle-{0:D4}" -f $frame)
            $timings.Add([pscustomobject]@{ Frame=$frame; TargetMilliseconds=$target
                CaptureStartMilliseconds=$before
                CaptureEndMilliseconds=$clock.Elapsed.TotalMilliseconds })
        }
        $timings | Export-Csv (Join-Path $OutputDir "timings.csv") `
            -NoTypeInformation -Encoding UTF8
    } else {
        Move-Relative -1200 -1200 4
        Start-Sleep -Milliseconds 1500
        Save-Pair "corner-tl-000"

        for ($frame = 1; $frame -le 40; $frame++) {
            Move-Relative 55 0
            Start-Sleep -Milliseconds 17
            Save-Pair ("sweep-x-{0:D3}" -f $frame)
        }
        Start-Sleep -Milliseconds 1000
        Save-Pair "corner-tr-000"

        Move-Relative -1200 1200 4
        Start-Sleep -Milliseconds 1200
        Save-Pair "corner-bl-000"
        for ($frame = 1; $frame -le 24; $frame++) {
            Move-Relative 95 -55
            Start-Sleep -Milliseconds 17
            Save-Pair ("sweep-diagonal-{0:D3}" -f $frame)
        }
        Start-Sleep -Milliseconds 1000
        Save-Pair "corner-tr-return-000"

        [MverParityInput]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 250
        Save-Pair "mouse-left-down-000"
        [MverParityInput]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 250
        Save-Pair "mouse-left-up-000"
        [MverParityInput]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 250
        Save-Pair "mouse-right-down-000"
        [MverParityInput]::mouse_event(16, 0, 0, 0, [UIntPtr]::Zero)

        if (-not $PointerOnly) {
            Shortcut 0x4C
            for ($frame = 1; $frame -le 12; $frame++) {
                Start-Sleep -Milliseconds 50
                Save-Pair ("expression-0-{0:D3}" -f $frame)
            }
            Shortcut 0x31
            for ($frame = 1; $frame -le 12; $frame++) {
                Start-Sleep -Milliseconds 50
                Save-Pair ("lock-motion-0-{0:D3}" -f $frame)
            }
        }
    }
} finally {
    [MverParityInput]::SetWindowPos($mver.MainWindowHandle, [IntPtr](-1),
        $mverOriginal.Left, $mverOriginal.Top,
        $mverOriginal.Right - $mverOriginal.Left,
        $mverOriginal.Bottom - $mverOriginal.Top, 0x40) | Out-Null
    [MverParityInput]::SetWindowPos($native.MainWindowHandle, [IntPtr](-1),
        $nativeOriginal.Left, $nativeOriginal.Top,
        $nativeOriginal.Right - $nativeOriginal.Left,
        $nativeOriginal.Bottom - $nativeOriginal.Top, 0x40) | Out-Null
    $background.Close()
    $background.Dispose()
}

$mverCount = @(Get-ChildItem -LiteralPath $mverDir -File).Count
$nativeCount = @(Get-ChildItem -LiteralPath $nativeDir -File).Count
$minimumFrames = if ($IdleOnly) {
    [int][Math]::Floor($IdleDurationSeconds * 1000.0 / $IdleIntervalMilliseconds) + 1
} elseif ($PointerOnly) { 71 } else { 95 }
if ($mverCount -ne $nativeCount -or $mverCount -lt $minimumFrames) {
    throw "Incomplete parity capture: mver=$mverCount native=$nativeCount"
}
Write-Output "Captured $mverCount synchronized frame pairs"
