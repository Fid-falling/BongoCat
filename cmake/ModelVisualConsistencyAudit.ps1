param([string]$Exe = "", [string]$OutputDir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) {
    $OutputDir = Join-Path $root "build-final\model-visual-consistency-test"
}
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$results = [Collections.Generic.List[object]]::new()
$diagnostic = 0
foreach ($model in @("standard", "keyboard", "gamepad")) {
    $data = Join-Path $OutputDir ("data-$model-" + [DateTime]::UtcNow.Ticks)
    $env:BONGO_CAT_TEST_INSTANCE_ID = "model-visual-$model-$PID"
    $arguments = @("--ci-smoke", "--ci-model=$model",
        "--ci-live2d-scenario=visual-consistency", "--ci-ignore-global-input",
        "--ci-exit-ms=1000", "--data-root=$data")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru -Wait
    $live2d = Join-Path $data "live2d-audit.txt"
    $visual = Join-Path $data "live2d-visual-audit.csv"
    $liveText = if (Test-Path $live2d) { Get-Content -Raw $live2d } else { "" }
    if ($liveText -match "renderer=diagnostic") { $diagnostic++; continue }
    $rows = if (Test-Path $visual) { @(Import-Csv $visual) } else { @() }
    $failed = @($rows | Where-Object { $_.passed -ne "1" })
    $required = @("idle", "pointer-0", "pointer-1",
        "expression-0-stable", "expression-1-stable", "expression-2-stable",
        "expression-0-scale-50", "expression-1-scale-200",
        "expression-2-mirror", "expression-2-reset", "result")
    $names = @($rows | ForEach-Object { $_.case })
    $missing = @($required | Where-Object { $_ -notin $names })
    $passed = $process.ExitCode -eq 0 -and
        $liveText -match "assertions=passed" -and -not $failed.Count -and
        -not $missing.Count
    if (Test-Path $visual) {
        Copy-Item $visual (Join-Path $OutputDir "$model.csv") -Force
    }
    $results.Add([pscustomobject]@{ Model=$model; ExitCode=$process.ExitCode
        Cases=$rows.Count; Failed=$failed.Count; Missing=($missing -join ",")
        Passed=$passed })
}
if ($diagnostic -eq 3) {
    Write-Output "Model visual consistency audit skipped: Cubism SDK is unavailable"
    exit 77
}
$results | ConvertTo-Json | Set-Content -Encoding UTF8 `
    (Join-Path $OutputDir "result.json")
$results | Format-Table -AutoSize
if ($diagnostic -or @($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
