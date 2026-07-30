param([string]$Exe = "", [string]$OutputDir = "", [int]$ScaleDelta = 120,
    [int]$BurstCount = 1, [int]$BurstDelayMs = 0, [switch]$AtEdge, [switch]$GlobalControl,
    [switch]$SystemWheel, [switch]$ControlOpacity, [string]$DataRoot = "",
    [ValidateRange(10, 100)][double]$InitialOpacity = 50)
. (Join-Path $PSScriptRoot "WheelAuditHelpers.ps1")
. (Join-Path $PSScriptRoot "WheelAuditStart.ps1")
try {
    $window = Wait-Window $process
    Start-Sleep -Milliseconds 400
    $initial = Get-Rect $window
    $workArea = [BongoCatWheelNative+Rect]::new()
    $workAreaAvailable = [BongoCatWheelNative]::SystemParametersInfoW(
        0x0030, 0, [ref]$workArea, 0)
    if ($AtEdge -and $workAreaAvailable) {
        [void][BongoCatWheelNative]::SetWindowPos($window, [IntPtr]::Zero,
            $workArea.R - ($initial.R - $initial.L),
            $workArea.B - ($initial.B - $initial.T), 0, 0, 0x0015)
        Start-Sleep -Milliseconds 100
        $initial = Get-Rect $window
    }
    $position = Position-LParam $initial
    $opacitySamples = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt 30; $index++) {
        [uint32]$sampleColor = 0; [byte]$sampleAlpha = 255; [uint32]$sampleFlags = 0
        [void][BongoCatWheelNative]::GetLayeredWindowAttributes(
            $window, [ref]$sampleColor, [ref]$sampleAlpha, [ref]$sampleFlags)
        $opacitySamples.Add([pscustomobject]@{Index=$index; Alpha=$sampleAlpha})
        Start-Sleep -Milliseconds 16
    }
    [uint32]$color = 0; [byte]$alpha = 255; [uint32]$flags = 0
    $opacityAvailable = [BongoCatWheelNative]::GetLayeredWindowAttributes(
        $window, [ref]$color, [ref]$alpha, [ref]$flags)

    $foregroundSeparated = $false
    if ($GlobalControl) {
        $shell = [BongoCatWheelNative]::GetShellWindow()
        if ($shell -ne [IntPtr]::Zero) {
            [void][BongoCatWheelNative]::SetForegroundWindow($shell)
            Start-Sleep -Milliseconds 120
        }
        $foregroundSeparated = [BongoCatWheelNative]::GetForegroundWindow() -ne $window
    }
    if ($ControlOpacity) {
        [BongoCatWheelNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        $globalControlDown = $true; Start-Sleep -Milliseconds 80
    } elseif (-not $GlobalControl) {
        [void][BongoCatWheelNative]::PostMessageW(
            $window, 0x0101, [IntPtr]0x11, [IntPtr]::Zero)
    }
    $process.Refresh()
    $initialWorkingSet = $process.WorkingSet64
    $initialCpu = $process.TotalProcessorTime.TotalMilliseconds
    $peakWorkingSet = $initialWorkingSet
    $initialFrames = if (Test-Path -LiteralPath $frameSeries) {
        @(Import-Csv -LiteralPath $frameSeries)
    } else { @() }
    $frameStartCount = $initialFrames.Count
    $initialRecordedOpacity = if ($initialFrames.Count -and
        $null -ne $initialFrames[-1].opacity_percent) {
        [double]$initialFrames[-1].opacity_percent
    } else { [double]$InitialOpacity }
    $initialRenderWidth = if ($initialFrames.Count) {
        [double]$initialFrames[-1].width
    } else { 0.0 }
    $initialRenderHeight = if ($initialFrames.Count) {
        [double]$initialFrames[-1].height
    } else { 0.0 }
    $wheelKeys = if ($ControlOpacity) { 8 } else { 0 }
    if ($SystemWheel) {
        [void][BongoCatWheelNative]::SetCursorPos(
            [int](($initial.L + $initial.R) / 2), [int](($initial.T + $initial.B) / 2))
        Start-Sleep -Milliseconds 80
    }
    for ($index = 0; $index -lt [Math]::Max(1, $BurstCount); $index++) {
        if ($SystemWheel) {
            [BongoCatWheelNative]::mouse_event(
                0x0800, 0, 0, $ScaleDelta, [UIntPtr]::Zero)
        } else {
            [void][BongoCatWheelNative]::PostMessageW($window, 0x020A,
                (Wheel-WParam $ScaleDelta $wheelKeys), $position)
        }
        if ($BurstDelayMs -gt 0) { Start-Sleep -Milliseconds $BurstDelayMs }
    }
    $samples = [Collections.Generic.List[object]]::new()
    $sampleClock = [Diagnostics.Stopwatch]::StartNew()
    for ($index = 0; $index -lt 30; $index++) {
        $rect = Get-Rect $window
        $process.Refresh()
        if ($process.WorkingSet64 -gt $peakWorkingSet) {
            $peakWorkingSet = $process.WorkingSet64
        }
        $samples.Add([pscustomobject]@{ElapsedMs=$sampleClock.Elapsed.TotalMilliseconds
            Width=$rect.R-$rect.L; Height=$rect.B-$rect.T
            CenterX=($rect.L+$rect.R)/2.0; CenterY=($rect.T+$rect.B)/2.0})
        Start-Sleep -Milliseconds 16
    }
    if ($ControlOpacity) {
        [BongoCatWheelNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
        $globalControlDown = $false
    } elseif (-not $GlobalControl) {
        [void][BongoCatWheelNative]::PostMessageW(
            $window, 0x0101, [IntPtr]0x11, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
    $visiblePixels = Get-VisiblePixels (Join-Path $data "frame.bmp")
    $frameRows = if (Test-Path -LiteralPath $frameSeries) {
        @(@(Import-Csv -LiteralPath $frameSeries) | Select-Object -Skip $frameStartCount)
    } else { @() }
    $minimumFramePixels = if ($frameRows.Count) {
        ($frameRows | Measure-Object -Property visible_pixels -Minimum).Minimum
    } else { 0 }
    $uniqueRenderWidths = @($frameRows.width | Select-Object -Unique).Count
    $finalRenderWidth = if ($frameRows.Count) {
        [double]$frameRows[-1].width
    } else { $initialRenderWidth }
    $finalRenderHeight = if ($frameRows.Count) {
        [double]$frameRows[-1].height
    } else { $initialRenderHeight }
    $opacityRows = @($frameRows | Where-Object { $null -ne $_.opacity_percent })
    $uniqueRenderOpacities = @($opacityRows.opacity_percent | Select-Object -Unique).Count
    $finalRecordedOpacity = if ($opacityRows.Count) {
        [double]$opacityRows[-1].opacity_percent
    } else { $initialRecordedOpacity }
    $finalWindowOpacity = if ($opacityRows.Count -and
        $null -ne $opacityRows[-1].window_opacity) {
        100.0 * [double]$opacityRows[-1].window_opacity
    } else { $initialRecordedOpacity }
    $maxNormalizedRenderWidthStep = 0.0
    $maxRenderWidthStep = 0.0
    $oppositeRenderSteps = 0
    $normalizedRenderSteps = [Collections.Generic.List[double]]::new()
    $renderIntervals = [Collections.Generic.List[double]]::new()
    for ($index = 1; $index -lt $frameRows.Count; $index++) {
        $signedStep = [double]$frameRows[$index].width -
            [double]$frameRows[$index - 1].width
        $step = [Math]::Abs($signedStep)
        $elapsed = ([double]$frameRows[$index].ticks_ns -
            [double]$frameRows[$index - 1].ticks_ns) / 1000000.0
        if ($elapsed -gt 0) {
            $renderIntervals.Add($elapsed)
            $normalized = $step * 16.6667 / $elapsed
            if ($step -gt 0) { $normalizedRenderSteps.Add($normalized) }
            if ($normalized -gt $maxNormalizedRenderWidthStep) {
                $maxNormalizedRenderWidthStep = $normalized
            }
        }
        if ($step -gt $maxRenderWidthStep) { $maxRenderWidthStep = $step }
        if (($ScaleDelta -gt 0 -and $signedStep -lt 0) -or
            ($ScaleDelta -lt 0 -and $signedStep -gt 0)) { $oppositeRenderSteps++ }
    }
    $sortedSteps = @($normalizedRenderSteps | Sort-Object)
    $sortedIntervals = @($renderIntervals | Sort-Object)
    $stepP95 = if ($sortedSteps.Count) { $sortedSteps[
        [Math]::Ceiling(($sortedSteps.Count - 1) * 0.95)] } else { 0.0 }
    $intervalP95 = if ($sortedIntervals.Count) { $sortedIntervals[
        [Math]::Ceiling(($sortedIntervals.Count - 1) * 0.95)] } else { 0.0 }
    $maximumInterval = if ($sortedIntervals.Count) { $sortedIntervals[-1] } else { 0.0 }
    $firstRenderWidth = if ($frameRows.Count) { [double]$frameRows[0].width } else { 0.0 }
    $maxRenderWidthStepPercent = if ($firstRenderWidth -gt 0) {
        100.0 * $maxRenderWidthStep / $firstRenderWidth
    } else { 0.0 }
    if (Test-Path -LiteralPath $frameSeries) {
        Copy-Item -LiteralPath $frameSeries -Destination `
            (Join-Path $OutputDir "frame-series.csv") -Force
    }
    $process.Refresh()
    $final = $samples[$samples.Count - 1]
    $opacitySamples | Export-Csv (Join-Path $OutputDir "opacity-samples.csv") -NoTypeInformation
    $samples | Export-Csv (Join-Path $OutputDir "scale-samples.csv") -NoTypeInformation
    $uniqueWidths = @($samples.Width | Select-Object -Unique).Count
    $maxWidthStep = 0
    $maxNormalizedWidthStep = 0.0
    $settledAtMs = 0.0
    for ($index = 1; $index -lt $samples.Count; $index++) {
        $step = [Math]::Abs($samples[$index].Width - $samples[$index - 1].Width)
        if ($step -gt 0) { $settledAtMs = $samples[$index].ElapsedMs }
        if ($step -gt $maxWidthStep) { $maxWidthStep = $step }
        $elapsed = $samples[$index].ElapsedMs - $samples[$index - 1].ElapsedMs
        if ($elapsed -gt 0) {
            $normalized = $step * 16.6667 / $elapsed
            if ($normalized -gt $maxNormalizedWidthStep) {
                $maxNormalizedWidthStep = $normalized
            }
        }
    }
    $centerX = ($initial.L + $initial.R) / 2.0
    $centerY = ($initial.T + $initial.B) / 2.0
    $maximumCenterDriftX = ($samples | ForEach-Object {
        [Math]::Abs($_.CenterX - $centerX) } | Measure-Object -Maximum).Maximum
    $maximumCenterDriftY = ($samples | ForEach-Object {
        [Math]::Abs($_.CenterY - $centerY) } | Measure-Object -Maximum).Maximum
    $initialArea = ($initial.R - $initial.L) * ($initial.B - $initial.T)
    $finalArea = $final.Width * $final.Height
    $frameAreaRatio = if ($initialArea -gt 0) { $finalArea / [double]$initialArea } else { 1.0 }
    $allowedWorkingSetGrowthMB = 64.0 * [Math]::Max(1.0, $frameAreaRatio)
    $result = [ordered]@{
        OpacityAvailable=$opacityAvailable; OpacityAlpha=$alpha
        InitialOpacityPercent=$initialRecordedOpacity
        FinalOpacityPercent=$finalRecordedOpacity
        FinalWindowOpacityPercent=$finalWindowOpacity
        UniqueAnimatedOpacities=$uniqueRenderOpacities
        InitialRenderWidth=$initialRenderWidth; FinalRenderWidth=$finalRenderWidth
        InitialRenderHeight=$initialRenderHeight; FinalRenderHeight=$finalRenderHeight
        InitialWidth=$initial.R-$initial.L; FinalWidth=$final.Width
        InitialHeight=$initial.B-$initial.T; FinalHeight=$final.Height
        UniqueAnimatedWidths=$uniqueWidths
        MaxAnimatedWidthStep=$maxWidthStep
        MaxNormalizedWidthStep=$maxNormalizedWidthStep
        CenterDriftX=[Math]::Abs($final.CenterX-$centerX)
        CenterDriftY=[Math]::Abs($final.CenterY-$centerY)
        MaximumCenterDriftX=$maximumCenterDriftX
        MaximumCenterDriftY=$maximumCenterDriftY
        SettledAtMs=$settledAtMs
        BurstCount=$BurstCount
        BurstDelayMs=$BurstDelayMs
        GlobalControl=$GlobalControl.IsPresent
        SystemWheel=$SystemWheel.IsPresent
        ForegroundSeparated=(-not $GlobalControl -or $foregroundSeparated)
        PeakWorkingSetMB=$peakWorkingSet / 1MB
        WorkingSetGrowthMB=($peakWorkingSet-$initialWorkingSet) / 1MB
        FrameAreaRatio=$frameAreaRatio
        AllowedWorkingSetGrowthMB=$allowedWorkingSetGrowthMB
        ScaleCpuMs=$process.TotalProcessorTime.TotalMilliseconds-$initialCpu
        VisibleFramePixels=$visiblePixels
        CapturedRenderFrames=$frameRows.Count
        UniqueRenderWidths=$uniqueRenderWidths
        MaxNormalizedRenderWidthStep=$maxNormalizedRenderWidthStep
        NormalizedRenderWidthStepP95=$stepP95
        MaxRenderWidthStep=$maxRenderWidthStep
        MaxRenderWidthStepPercent=$maxRenderWidthStepPercent
        OppositeRenderSteps=$oppositeRenderSteps
        ChangedRenderFrames=$normalizedRenderSteps.Count
        RenderIntervalP95Ms=$intervalP95
        MaximumRenderIntervalMs=$maximumInterval
        MinimumFramePixels=[int]$minimumFramePixels
        WithinWorkArea=(-not $AtEdge -or -not $workAreaAvailable -or
            ($final.CenterX-$final.Width/2 -ge $workArea.L -and
            $final.CenterY-$final.Height/2 -ge $workArea.T -and
            $final.CenterX+$final.Width/2 -le $workArea.R -and
            $final.CenterY+$final.Height/2 -le $workArea.B))
    }
    $shortBurst = $BurstCount -le 3
    $expectedOpacity = if ($ControlOpacity) {
        [Math]::Max(10.0, [Math]::Min(100.0,
            $initialRecordedOpacity + [Math]::Sign($ScaleDelta) * 5.0 *
            [Math]::Max(1, $BurstCount)))
    } else { $initialRecordedOpacity }
    $result.ExpectedOpacityPercent = $expectedOpacity
    $renderDirectionPassed = $ControlOpacity -or
        ($ScaleDelta -gt 0 -and $finalRenderWidth -gt $initialRenderWidth -and
        $finalRenderHeight -gt $initialRenderHeight) -or
        ($ScaleDelta -lt 0 -and $finalRenderWidth -lt $initialRenderWidth -and
        $finalRenderHeight -lt $initialRenderHeight)
    $result.RenderDirectionPassed = $renderDirectionPassed
    $directionPassed = if ($ControlOpacity) {
        $final.Width -eq $result.InitialWidth -and
            $final.Height -eq $result.InitialHeight -and
            [Math]::Abs($finalRecordedOpacity - $expectedOpacity) -le 0.1 -and
            [Math]::Abs($finalWindowOpacity - $expectedOpacity) -le 1.0
    } elseif ($ScaleDelta -gt 0) {
        $final.Width -gt $result.InitialWidth -and $final.Height -gt $result.InitialHeight -and
            (-not $shortBurst -or $final.Width -le $result.InitialWidth * 1.2) -and
            $renderDirectionPassed
    } else {
        $final.Width -lt $result.InitialWidth -and $final.Height -lt $result.InitialHeight -and
            (-not $shortBurst -or $final.Width -ge $result.InitialWidth * 0.8) -and
            $renderDirectionPassed
    }
    $centerPassed = $AtEdge -or
        ($result.MaximumCenterDriftX -le 2 -and $result.MaximumCenterDriftY -le 2)
    $animationPassed = if ($ControlOpacity) {
        $uniqueRenderOpacities -ge 2
    } else { $uniqueWidths -ge 4 -or $uniqueRenderWidths -ge 4 }
    $smoothnessPassed = if ($ControlOpacity) {
        $uniqueRenderWidths -eq 1 -and $result.MaximumCenterDriftX -le 0.5 -and
            $result.MaximumCenterDriftY -le 0.5
    } elseif ($frameRows.Count -ge 2) {
        $maxNormalizedRenderWidthStep -le 20 -and $stepP95 -le 16 -and
            $maxRenderWidthStepPercent -le 3.0 -and $oppositeRenderSteps -eq 0
    } else { $maxNormalizedWidthStep -le 8 }
    $latencyPassed = $ControlOpacity -or -not $shortBurst -or $settledAtMs -le 350
    $result.Passed = $directionPassed -and
        $animationPassed -and $smoothnessPassed -and $latencyPassed -and $centerPassed -and
        $result.WithinWorkArea -and
        $result.WorkingSetGrowthMB -le $result.AllowedWorkingSetGrowthMB -and
        $result.ScaleCpuMs -le 1000 -and $result.ForegroundSeparated -and
        $result.VisibleFramePixels -ge 100 -and $result.CapturedRenderFrames -ge 2 -and
        $result.MinimumFramePixels -ge 100
    $result | ConvertTo-Json | Set-Content (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    if ($globalControlDown) {
        [BongoCatWheelNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    }
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
