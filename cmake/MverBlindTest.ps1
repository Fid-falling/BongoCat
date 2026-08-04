param(
    [Parameter(Mandatory=$true)][string]$MverFrames,
    [Parameter(Mandatory=$true)][string]$NativeFrames,
    [string]$OutputDir = "",
    [int]$Seed = 0,
    [int]$MaxFrameOffset = 2,
    [int]$AlphaThreshold = 8,
    [double]$MinimumSimilarity = 99.0,
    [double]$MinimumWithinEight = 98.0,
    [double]$MinimumForegroundSimilarity = 98.0,
    [double]$MinimumForegroundWithinEight = 95.0,
    [string]$MaskFrames = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$MverFrames = [IO.Path]::GetFullPath($MverFrames)
$NativeFrames = [IO.Path]::GetFullPath($NativeFrames)
if ($MaskFrames) { $MaskFrames = [IO.Path]::GetFullPath($MaskFrames) }
if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path $PSScriptRoot -Parent) "build\mver-blind-test"
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (-not (Test-Path -LiteralPath $MverFrames -PathType Container)) {
    throw "Missing Mver frame directory: $MverFrames"
}
if (-not (Test-Path -LiteralPath $NativeFrames -PathType Container)) {
    throw "Missing native frame directory: $NativeFrames"
}
if ($MaskFrames -and -not (Test-Path -LiteralPath $MaskFrames -PathType Container)) {
    throw "Missing foreground mask directory: $MaskFrames"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$caseDir = Join-Path $OutputDir "cases"
$privateDir = Join-Path $OutputDir "private"
New-Item -ItemType Directory -Force -Path $caseDir,$privateDir | Out-Null

function Get-Frames([string]$Directory) {
    $result = @{}
    Get-ChildItem -LiteralPath $Directory -File | Where-Object {
        $_.Extension -in @(".png", ".bmp")
    } | ForEach-Object {
        if ($result.ContainsKey($_.BaseName)) {
            throw "Duplicate frame name '$($_.BaseName)' in $Directory"
        }
        $result[$_.BaseName] = $_.FullName
    }
    return $result
}

function Measure-Pair([string]$Left, [string]$Right, [string]$Mask = "") {
    $a = [Drawing.Bitmap]::new($Left)
    $b = [Drawing.Bitmap]::new($Right)
    $maskBitmap = if ($Mask) { [Drawing.Bitmap]::new($Mask) } else { $null }
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
            throw "Frame dimensions differ: $Left and $Right"
        }
        $step = [Math]::Max(1, [int][Math]::Sqrt(
            ($a.Width * [double]$a.Height) / 250000.0))
        $absolute = 0.0; $within = 0; $samples = 0
        $foregroundAbsolute = 0.0; $foregroundWithin = 0; $foreground = 0
        $alphaUnion = 0; $alphaIntersection = 0; $alphaVaries = $false
        for ($y = 0; $y -lt $a.Height; $y += $step) {
            for ($x = 0; $x -lt $a.Width; $x += $step) {
                $first = $a.GetPixel($x, $y); $second = $b.GetPixel($x, $y)
                $maximum = 0
                foreach ($delta in @(
                    [Math]::Abs($first.R - $second.R),
                    [Math]::Abs($first.G - $second.G),
                    [Math]::Abs($first.B - $second.B),
                    [Math]::Abs($first.A - $second.A))) {
                    $absolute += $delta; $maximum = [Math]::Max($maximum, $delta)
                }
                if ($maximum -le 8) { $within++ }
                $firstVisible = $first.A -gt $AlphaThreshold
                $secondVisible = $second.A -gt $AlphaThreshold
                if ($first.A -lt 255 -or $second.A -lt 255) { $alphaVaries = $true }
                if ($firstVisible -or $secondVisible) { $alphaUnion++ }
                if ($firstVisible -and $secondVisible) { $alphaIntersection++ }
                $isForeground = $firstVisible -or $secondVisible
                if ($maskBitmap) {
                    $maskX = [Math]::Min($maskBitmap.Width - 1,
                        [int]($x * [double]$maskBitmap.Width / $a.Width))
                    $maskY = [Math]::Min($maskBitmap.Height - 1,
                        [int]($y * [double]$maskBitmap.Height / $a.Height))
                    $maskPixel = $maskBitmap.GetPixel($maskX, $maskY)
                    $isForeground = $maskPixel.A -gt $AlphaThreshold -or
                        [Math]::Max($maskPixel.R,
                            [Math]::Max($maskPixel.G, $maskPixel.B)) -gt $AlphaThreshold
                } elseif (-not $alphaVaries) {
                    $isForeground = $true
                }
                if ($isForeground) {
                    $foregroundMaximum = 0
                    foreach ($delta in @(
                        [Math]::Abs($first.R - $second.R),
                        [Math]::Abs($first.G - $second.G),
                        [Math]::Abs($first.B - $second.B),
                        [Math]::Abs($first.A - $second.A))) {
                        $foregroundAbsolute += $delta
                        $foregroundMaximum = [Math]::Max($foregroundMaximum, $delta)
                    }
                    if ($foregroundMaximum -le 8) { $foregroundWithin++ }
                    $foreground++
                }
                $samples++
            }
        }
        if (-not $foreground) { throw "No foreground pixels found for $Left" }
        $alphaUsable = $alphaVaries -and $alphaUnion -lt $samples * 0.98
        return [pscustomobject]@{
            Width=$a.Width; Height=$a.Height
            Similarity=100.0 * (1.0 - $absolute / ($samples * 4.0 * 255.0))
            WithinEight=100.0 * $within / $samples
            ForegroundSimilarity=100.0 * (1.0 -
                $foregroundAbsolute / ($foreground * 4.0 * 255.0))
            ForegroundWithinEight=100.0 * $foregroundWithin / $foreground
            ForegroundSamples=$foreground
            ForegroundMode=if ($maskBitmap) { "mask" }
                elseif ($alphaUsable) { "alpha-union" } else { "full-frame-fallback" }
            AlphaIoU=if ($alphaUsable -and $alphaUnion) {
                100.0 * $alphaIntersection / $alphaUnion
            } else { 100.0 }
        }
    } finally {
        if ($maskBitmap) { $maskBitmap.Dispose() }
        $a.Dispose(); $b.Dispose()
    }
}

function Get-FrameIdentity([string]$Name) {
    if ($Name -match '^(.*?)(\d+)$') {
        return [pscustomobject]@{ Prefix=$Matches[1]; Number=[int64]$Matches[2] }
    }
    return [pscustomobject]@{ Prefix=$Name; Number=$null }
}

function Get-Candidates([string]$Name, [hashtable]$Frames) {
    $identity = Get-FrameIdentity $Name
    if ($null -eq $identity.Number -or $MaxFrameOffset -le 0) {
        return @($Name | Where-Object { $Frames.ContainsKey($_) })
    }
    return @($Frames.Keys | Where-Object {
        $candidate = Get-FrameIdentity $_
        $candidate.Prefix -eq $identity.Prefix -and $null -ne $candidate.Number -and
            [Math]::Abs($candidate.Number - $identity.Number) -le $MaxFrameOffset
    } | Sort-Object)
}

function Save-BlindPair([string]$Left, [string]$Right, [string]$Label,
    [string]$Path) {
    $a = [Drawing.Image]::FromFile($Left); $b = [Drawing.Image]::FromFile($Right)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
            throw "Blind pair dimensions differ for $Label"
        }
        $header = 36
        $sheet = [Drawing.Bitmap]::new($a.Width * 2, $a.Height + $header)
        $graphics = [Drawing.Graphics]::FromImage($sheet)
        try {
            $graphics.Clear([Drawing.Color]::FromArgb(28, 30, 34))
            $graphics.DrawImageUnscaled($a, 0, $header)
            $graphics.DrawImageUnscaled($b, $a.Width, $header)
            $font = [Drawing.Font]::new("Segoe UI", 12, [Drawing.FontStyle]::Bold)
            try {
                $graphics.DrawString("$Label   A", $font, [Drawing.Brushes]::White, 8, 7)
                $graphics.DrawString("$Label   B", $font, [Drawing.Brushes]::White,
                    $a.Width + 8, 7)
            } finally { $font.Dispose() }
            $sheet.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $sheet.Dispose() }
    } finally { $a.Dispose(); $b.Dispose() }
}

$mver = Get-Frames $MverFrames
$native = Get-Frames $NativeFrames
$masks = if ($MaskFrames) { Get-Frames $MaskFrames } else { @{} }
$names = @($mver.Keys | Where-Object { $native.ContainsKey($_) } | Sort-Object)
if (-not $names.Count) { throw "No matching frame names were found" }
if ($Seed -eq 0) { $Seed = [Environment]::TickCount -band 0x7fffffff }
$random = [Random]::new($Seed)
$names = @($names | Sort-Object { $random.Next() })
$metrics = [Collections.Generic.List[object]]::new()
$keys = [Collections.Generic.List[object]]::new()
$ballot = [Collections.Generic.List[object]]::new()
$passed = $true
for ($index = 0; $index -lt $names.Count; $index++) {
    $name = $names[$index]; $case = "Case-{0:D3}" -f ($index + 1)
    $bestName = $null; $measure = $null
    foreach ($candidate in @(Get-Candidates $name $native)) {
        $mask = if ($masks.ContainsKey($candidate)) { $masks[$candidate] } else { "" }
        $candidateMeasure = Measure-Pair $mver[$name] $native[$candidate] $mask
        if (-not $measure -or $candidateMeasure.ForegroundSimilarity -gt
            $measure.ForegroundSimilarity) {
            $bestName = $candidate; $measure = $candidateMeasure
        }
    }
    if (-not $measure) { throw "No native candidate for frame '$name'" }
    $mverSide = if ($random.Next(2) -eq 0) { "A" } else { "B" }
    $left = if ($mverSide -eq "A") { $mver[$name] } else { $native[$bestName] }
    $right = if ($mverSide -eq "B") { $mver[$name] } else { $native[$bestName] }
    $casePassed = $measure.Similarity -ge $MinimumSimilarity -and
        $measure.WithinEight -ge $MinimumWithinEight -and
        $measure.ForegroundSimilarity -ge $MinimumForegroundSimilarity -and
        $measure.ForegroundWithinEight -ge $MinimumForegroundWithinEight
    $passed = $passed -and $casePassed
    Save-BlindPair $left $right $case (Join-Path $caseDir "$case.png")
    $metrics.Add([pscustomobject]@{ Case=$case; Width=$measure.Width
        Height=$measure.Height; SimilarityPercent=$measure.Similarity
        WithinEightPercent=$measure.WithinEight
        ForegroundSimilarityPercent=$measure.ForegroundSimilarity
        ForegroundWithinEightPercent=$measure.ForegroundWithinEight
        ForegroundSamples=$measure.ForegroundSamples
        ForegroundMode=$measure.ForegroundMode; AlphaIoUPercent=$measure.AlphaIoU
        NativeFrame=$bestName; Passed=$casePassed })
    $keys.Add([pscustomobject]@{ Case=$case; SourceFrame=$name
        NativeFrame=$bestName; MverSide=$mverSide })
    $ballot.Add([pscustomobject]@{ Case=$case; Pick=""; Confidence=""; Notes="" })
}
$metrics | Export-Csv (Join-Path $OutputDir "metrics.csv") -NoTypeInformation -Encoding UTF8
$ballot | Export-Csv (Join-Path $OutputDir "ballot.csv") -NoTypeInformation -Encoding UTF8
[pscustomobject]@{ Seed=$Seed; Cases=$keys } | ConvertTo-Json -Depth 4 |
    Set-Content (Join-Path $privateDir "answer-key.json") -Encoding UTF8
[pscustomobject]@{ Cases=$names.Count; MinimumSimilarity=$MinimumSimilarity
    MinimumWithinEight=$MinimumWithinEight
    MinimumForegroundSimilarity=$MinimumForegroundSimilarity
    MinimumForegroundWithinEight=$MinimumForegroundWithinEight
    MaxFrameOffset=$MaxFrameOffset; Passed=$passed } | ConvertTo-Json |
    Set-Content (Join-Path $OutputDir "result.json") -Encoding UTF8
Write-Output "Blind test: $($names.Count) cases; seed=$Seed; passed=$passed"
if (-not $passed) { exit 1 }
