function Save-ContactSheet([object[]]$Rows, [string]$Path, [int]$Columns = 3) {
    if (-not $Rows.Count) { return }
    $cellWidth = 459
    $cellHeight = 372
    $rowCount = [Math]::Ceiling($Rows.Count / $Columns)
    $sheet = [Drawing.Bitmap]::new($cellWidth * $Columns, $cellHeight * $rowCount)
    $graphics = [Drawing.Graphics]::FromImage($sheet)
    $graphics.Clear([Drawing.Color]::FromArgb(17, 20, 27))
    $font = [Drawing.Font]::new("Segoe UI", 11)
    $brush = [Drawing.Brushes]::White
    for ($index = 0; $index -lt $Rows.Count; $index++) {
        $x = ($index % $Columns) * $cellWidth
        $y = [Math]::Floor($index / $Columns) * $cellHeight
        $label = "$($Rows[$index].Theme) $($Rows[$index].Language) page $($Rows[$index].Page) $($Rows[$index].Model) $($Rows[$index].Scenario)"
        $graphics.DrawString($label.Trim(), $font, $brush, $x + 6, $y + 4)
        $image = [Drawing.Image]::FromFile($Rows[$index].Path)
        $graphics.DrawImage($image, $x, $y + 28, $cellWidth, 344)
        $image.Dispose()
    }
    $sheet.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    $font.Dispose()
    $graphics.Dispose()
    $sheet.Dispose()
}

function Stop-AuditProcess([Diagnostics.Process]$Process) {
    if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
    $Process.WaitForExit()
}

function Wait-Frame([string]$Path) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path $Path) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 20
    }
}

function Copy-Frame([string]$Source, [string]$Destination) {
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        try { Copy-Item $Source $Destination -Force; return }
        catch [IO.IOException] { Start-Sleep -Milliseconds 25 }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out copying completed framebuffer: $Source"
}

function Test-Live2DLog([string]$Path) {
    if (-not (Test-Path $Path)) { return $false }
    $text = Get-Content -Raw -LiteralPath $Path
    return $text -match 'renderer=cubism-native' -and
        $text -match 'operation=accepted' -and
        $text -match 'assertions=passed' -and
        $text -notmatch 'visual-model-result=blocked'
}

function Measure-ImageDifference([string]$Left, [string]$Right) {
    if (-not (Test-Path $Left) -or -not (Test-Path $Right)) { return 0.0 }
    $a = [Drawing.Bitmap]::new($Left)
    $b = [Drawing.Bitmap]::new($Right)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) { return 1.0 }
        $changed = 0
        $samples = 0
        for ($y = 0; $y -lt $a.Height; $y += 3) {
            for ($x = 0; $x -lt $a.Width; $x += 3) {
                $first = $a.GetPixel($x, $y)
                $second = $b.GetPixel($x, $y)
                $delta = [Math]::Abs($first.R - $second.R) +
                    [Math]::Abs($first.G - $second.G) +
                    [Math]::Abs($first.B - $second.B) +
                    [Math]::Abs($first.A - $second.A)
                if ($delta -gt 12) { $changed++ }
                $samples++
            }
        }
        return $changed / [double]$samples
    } finally { $a.Dispose(); $b.Dispose() }
}

function Measure-ModelAppearance([string]$Path) {
    if (-not (Test-Path $Path)) { return 0 }
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $bright = 0
        for ($y = 15; $y -lt [Math]::Min(210, $bitmap.Height); $y += 3) {
            for ($x = 90; $x -lt [Math]::Min(525, $bitmap.Width); $x += 3) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.A -ge 128 -and $pixel.R + $pixel.G + $pixel.B -ge 600) { $bright++ }
            }
        }
        return $bright
    } finally { $bitmap.Dispose() }
}
