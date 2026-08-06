param(
    [Parameter(Mandatory=$true)][string]$MverFrames,
    [Parameter(Mandatory=$true)][string]$NativeFrames,
    [string]$OutputDir = "",
    [double]$MaxLagSeconds = 5.0,
    [int]$MinimumOverlapFrames = 60,
    [int]$IntervalMilliseconds = 200
)

$ErrorActionPreference = "Stop"
if ($MaxLagSeconds -lt 0 -or $MinimumOverlapFrames -lt 2 -or
    $IntervalMilliseconds -le 0) {
    throw "Lag, overlap, and capture interval parameters are invalid"
}
Add-Type -Path (Join-Path $PSScriptRoot "MverPhaseMetrics.cs") `
    -ReferencedAssemblies System.Drawing
$MverFrames = [IO.Path]::GetFullPath($MverFrames)
$NativeFrames = [IO.Path]::GetFullPath($NativeFrames)
if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path $PSScriptRoot -Parent) "build\mver-phase-audit"
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Get-Frames([string]$Directory) {
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw "Missing frame directory: $Directory"
    }
    return @(
        Get-ChildItem -LiteralPath $Directory -File | Where-Object {
            $_.Extension -in @(".png", ".bmp")
        } | Sort-Object BaseName | ForEach-Object { $_.FullName }
    )
}

$mver = Get-Frames $MverFrames
$native = Get-Frames $NativeFrames
$maxLagFrames = [int][Math]::Round(
    $MaxLagSeconds * 1000.0 / $IntervalMilliseconds)
$availableLag = [Math]::Min($mver.Count, $native.Count) - $MinimumOverlapFrames
if ($availableLag -lt 0) {
    throw "Lag analysis needs $MinimumOverlapFrames frames; mver=$($mver.Count) native=$($native.Count)"
}
$maxLagFrames = [Math]::Min($maxLagFrames, $availableLag)
$candidates = [MverPhaseMetrics]::Search(
    [string[]]$mver, [string[]]$native, $maxLagFrames,
    $MinimumOverlapFrames, $IntervalMilliseconds)
$candidates | Export-Csv (Join-Path $OutputDir "phase-candidates.csv") `
    -NoTypeInformation -Encoding UTF8
$best = $candidates[0]
$zero = @($candidates | Where-Object { $_.NativeOffsetFrames -eq 0 })[0]
$result = [pscustomobject]@{
    Method="non-circular-frame-difference-lag"
    MaxLagSeconds=$MaxLagSeconds
    IntervalMilliseconds=$IntervalMilliseconds
    MinimumOverlapFrames=$MinimumOverlapFrames
    NativeOffsetFrames=$best.NativeOffsetFrames
    NativeOffsetSeconds=$best.NativeOffsetSeconds
    OverlapFrames=$best.OverlapFrames
    FullSimilarityPercent=$best.FullSimilarity
    FaceSimilarityPercent=$best.FaceSimilarity
    HairSimilarityPercent=$best.HairSimilarity
    HandSimilarityPercent=$best.HandSimilarity
    FullMotionSimilarityPercent=$best.FullMotionSimilarity
    FaceMotionSimilarityPercent=$best.FaceMotionSimilarity
    HairMotionSimilarityPercent=$best.HairMotionSimilarity
    HandMotionSimilarityPercent=$best.HandMotionSimilarity
    ZeroLagFullMotionSimilarityPercent=$zero.FullMotionSimilarity
    ZeroLagHairMotionSimilarityPercent=$zero.HairMotionSimilarity
}
$result | ConvertTo-Json | Set-Content (Join-Path $OutputDir "result.json") -Encoding UTF8
$result | Format-List
