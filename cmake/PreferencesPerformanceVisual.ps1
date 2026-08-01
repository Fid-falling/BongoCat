function Measure-Difference([string]$First, [string]$Second) {
    $a = [Drawing.Bitmap]::new($First); $b = [Drawing.Bitmap]::new($Second)
    try {
        $changed = 0; $samples = 0
        $step = [Math]::Max(1, [int][Math]::Round(4 * $script:UiScale))
        $top = [int][Math]::Round(60 * $script:UiScale)
        $left = [int][Math]::Round(155 * $script:UiScale)
        $right = [Math]::Min($a.Width, $b.Width) - $step
        $bottom = [Math]::Min($a.Height, $b.Height) - $step
        for ($y = $top; $y -lt $bottom; $y += $step) {
            for ($x = $left; $x -lt $right; $x += $step) {
                if ($a.GetPixel($x, $y).ToArgb() -ne
                    $b.GetPixel($x, $y).ToArgb()) { $changed++ }
                $samples++
            }
        }
        return $changed / [double]$samples
    } finally { $a.Dispose(); $b.Dispose() }
}

function Measure-BlackBottom([string]$Path) {
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $black = 0
        $left = [int]($bitmap.Width * .25); $right = [int]($bitmap.Width * .75)
        $top = [Math]::Max(0, $bitmap.Height -
            [int][Math]::Round(12 * $script:UiScale))
        for ($y = $top; $y -lt $bitmap.Height - 2; $y += 2) {
            for ($x = $left; $x -lt $right; $x += 3) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.R -le 8 -and $pixel.G -le 8 -and $pixel.B -le 8) {
                    $black++
                }
            }
        }
        return $black
    } finally { $bitmap.Dispose() }
}

function Invoke-LiveResizeAudit([IntPtr]$Window, [string]$FrameSeries) {
    $before = @(Get-Content -LiteralPath $FrameSeries `
        -ErrorAction SilentlyContinue).Count
    $capture = ""
    [void][BongoCatPreferencePerformanceNative]::SendMessageW(
        $Window, 0x0231, [UIntPtr]::Zero, [IntPtr]::Zero)
    try {
        for ($index = 0; $index -lt 24; $index++) {
            $width = [int][Math]::Round((720 + ($index % 8) * 32) * $script:UiScale)
            $height = [int][Math]::Round((560 + ($index % 6) * 18) * $script:UiScale)
            [void][BongoCatPreferencePerformanceNative]::SetWindowPos($Window,
                [IntPtr](-1), 40, 40, $width, $height, 0x0040)
            Start-Sleep -Milliseconds 25
            if ($index -eq 12) { $capture = Save-Screen $Window "resize-midpoint.png" }
        }
    } finally {
        [void][BongoCatPreferencePerformanceNative]::SendMessageW(
            $Window, 0x0232, [UIntPtr]::Zero, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 300
    $frames = @(Get-Content -LiteralPath $FrameSeries `
        -ErrorAction SilentlyContinue).Count - $before
    return [pscustomobject]@{ Frames=$frames; Capture=$capture }
}
