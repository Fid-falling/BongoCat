param([string]$Exe = "", [string]$OutputDir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\overlay-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$scenarios = @("standard:key-left", "keyboard:key-left", "keyboard:key-right",
    "keyboard:keys-both", "keyboard:key-left-release",
    "keyboard:keys-both-release", "keyboard:key-stress", "gamepad:gamepad-buttons")
& (Join-Path $PSScriptRoot "VisualAudit.ps1") -Exe $Exe -OutputDir $OutputDir `
    -SkipPreferences -Live2DScenarios $scenarios
# VisualAudit intentionally reports diagnostic Live2D as blocked; overlay evidence is separate.
$rows = @(Import-Csv (Join-Path $OutputDir "audit.csv") |
    Where-Object View -eq "live2d")
Add-Type -AssemblyName System.Drawing

function Measure-Frame([string]$Path) {
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $visible = 0
        $faceInk = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 3) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 3) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.R + $pixel.G + $pixel.B -gt 30) { $visible++ }
                if ($x -ge 225 -and $x -le 390 -and $y -ge 115 -and $y -le 205 -and
                    $pixel.R + $pixel.G + $pixel.B -lt 180) { $faceInk++ }
            }
        }
        return [pscustomobject]@{ Visible=$visible; FaceInk=$faceInk }
    } finally { $bitmap.Dispose() }
}

$results = foreach ($row in $rows) {
    $model = $row.Model
    $scenario = $row.Scenario
    $baselinePath = Join-Path $OutputDir "internal-main-$model.bmp"
    $framePath = Join-Path $OutputDir "internal-live2d-$model-$scenario.bmp"
    $baseline = Measure-Frame $baselinePath
    $frame = Measure-Frame $framePath
    $difference = [double]$row.Difference
    $release = $scenario.EndsWith("-release") -or $scenario -eq "key-stress"
    $visualRatio = $frame.Visible / [double]$baseline.Visible
    $faceRatio = $frame.FaceInk / [double]$baseline.FaceInk
    # Cubism breathing/eye updates continue between captures; allow a small
    # idle-animation delta while still staying well below a stuck key overlay.
    $differencePassed = if ($release) { $difference -le 0.03 } else {
        $difference -ge 0.005 -and $difference -le 0.12 }
    $passed = $differencePassed -and
        $visualRatio -ge 0.85 -and $faceRatio -ge 0.4
    [pscustomobject]@{ Model=$model; Scenario=$scenario
        Difference=[Math]::Round($difference, 6)
        VisibleRatio=[Math]::Round($visualRatio, 4)
        FaceInkRatio=[Math]::Round($faceRatio, 4); Passed=$passed }
}
$results | Export-Csv (Join-Path $OutputDir "overlay-audit.csv") `
    -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
if ($results.Count -ne $scenarios.Count -or
    @($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
