param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [ValidateRange(1, 20)][int]$WarmRuns = 7,
    [ValidateRange(1000, 15000)][int]$ExitMilliseconds = 2600
)

$ErrorActionPreference = "Stop"
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (-not [IO.File]::Exists($Exe)) { throw "Executable not found: $Exe" }
$runRoot = Join-Path $OutputDir ("run-" + [DateTime]::UtcNow.Ticks)
$dataRoot = Join-Path $runRoot "data"
[IO.Directory]::CreateDirectory($dataRoot) | Out-Null

function Measure-FirstFrame([string]$Name) {
    $frame = Join-Path $dataRoot "frame-alpha.txt"
    $previousWrite = if ([IO.File]::Exists($frame)) {
        [IO.File]::GetLastWriteTimeUtc($frame).Ticks
    } else { 0 }
    $arguments = @("--ci-smoke", "--ci-ignore-global-input",
        "--ci-exit-ms=$ExitMilliseconds", "`"--data-root=$dataRoot`"")
    $process = $null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) `
            -ArgumentList $arguments -WindowStyle Hidden -PassThru
        while ($true) {
            if ($process.HasExited) {
                throw "$Name exited before its first frame with code $($process.ExitCode)"
            }
            if ([IO.File]::Exists($frame) -and
                [IO.File]::GetLastWriteTimeUtc($frame).Ticks -gt $previousWrite) { break }
            if ($timer.ElapsedMilliseconds -gt 10000) { throw "$Name first-frame timeout" }
            Start-Sleep -Milliseconds 10
        }
        $timer.Stop()
        $frameAudit = [IO.File]::ReadAllText($frame)
        if ($frameAudit -notmatch "opaque=[1-9]" -or $frameAudit -notmatch "gl_error=0") {
            throw "$Name produced an invalid first frame: $frameAudit"
        }
        if (-not $process.WaitForExit($ExitMilliseconds + 7000)) {
            throw "$Name did not exit on schedule"
        }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "$Name failed with code $($process.ExitCode)" }
        return [double]$timer.ElapsedMilliseconds
    } finally {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
}

$priorAllow = $env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES
$priorId = $env:BONGO_CAT_NEO_TEST_INSTANCE_ID
$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "startup-performance-$PID"
try {
    $cold = Measure-FirstFrame "cold"
    $warm = [Collections.Generic.List[double]]::new()
    1..$WarmRuns | ForEach-Object {
        $warm.Add((Measure-FirstFrame ("warm-{0}" -f $_)))
    }
} finally {
    $env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = $priorAllow
    $env:BONGO_CAT_NEO_TEST_INSTANCE_ID = $priorId
}

$sorted = @($warm | Sort-Object)
$p95Index = [Math]::Ceiling($sorted.Count * 0.95) - 1
[pscustomobject]@{
    ColdFirstFrameMs = [Math]::Round($cold, 0)
    WarmRunsMs = ($warm | ForEach-Object { [Math]::Round($_, 0) }) -join ", "
    WarmAverageMs = [Math]::Round(($warm | Measure-Object -Average).Average, 0)
    WarmP95Ms = [Math]::Round($sorted[$p95Index], 0)
    DataRoot = $dataRoot
} | Format-List
