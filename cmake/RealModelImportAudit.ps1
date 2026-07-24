param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [string]$OutputDir = "",
    [string]$TauriSource = $env:BONGO_CAT_NEO_TAURI_SOURCE,
    [string]$MverSource = $env:BONGO_CAT_NEO_MVER_SOURCE,
    [string[]]$PatchSources = @()
)
$ErrorActionPreference = "Stop"
$Exe = [IO.Path]::GetFullPath($Exe)
if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path $Exe) "real-model-import-audit"
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
Remove-Item -LiteralPath $OutputDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
if (-not $PatchSources.Count -and $env:BONGO_CAT_NEO_MVER_PATCH_SOURCES) {
    $PatchSources = @($env:BONGO_CAT_NEO_MVER_PATCH_SOURCES -split ';' |
        Where-Object { $_ })
}

$cases = [Collections.Generic.List[object]]::new()
if ($TauriSource) { $cases.Add([pscustomobject]@{Name="tauri";Path=$TauriSource}) }
if ($MverSource) { $cases.Add([pscustomobject]@{Name="mver";Path=$MverSource}) }
for ($index = 0; $index -lt $PatchSources.Count; $index++) {
    $cases.Add([pscustomobject]@{Name="mver-patch-$($index+1)";Path=$PatchSources[$index]})
}
if (-not $cases.Count) {
    Write-Output "SKIP: real model sources were not configured"
    exit 0
}

function Start-Smoke([string[]]$Arguments) {
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) `
        -WindowStyle Hidden -PassThru -ArgumentList $Arguments
    $process.WaitForExit()
    return $process.ExitCode
}

$results = foreach ($case in $cases) {
    $data = Join-Path $OutputDir ("data-" + $case.Name)
    $exists = Test-Path -LiteralPath $case.Path -PathType Container
    $importExit = if ($exists) { Start-Smoke @("--ci-smoke", "--ci-exit-ms=1800",
        "--data-root=$data", "--ci-import=$($case.Path)") } else { -1 }
    $models = if ($exists) { @(Get-ChildItem (Join-Path $data "custom-models") `
        -Directory -ErrorAction SilentlyContinue | Where-Object Name -NotLike ".*") } else { @() }
    $reports = @($models | Where-Object {
        Test-Path (Join-Path $_.FullName ".bongo-cat-neo-import-report.json")
    }).Count
    $runtimePassed = $true
    foreach ($model in $models) {
        $exit = Start-Smoke @("--ci-smoke", "--ci-exit-ms=1200",
            "--ci-ignore-global-input", "--ci-live2d-scenario=idle",
            "--ci-model=$($model.Name)", "--data-root=$data")
        if ($exit -ne 0) { $runtimePassed = $false }
    }
    [pscustomobject]@{ Case=$case.Name; Source=$case.Path; Exists=$exists
        ImportExit=$importExit; Models=$models.Count; Reports=$reports
        RuntimePassed=$runtimePassed; Passed=$exists -and $importExit -eq 0 -and
            $models.Count -eq 3 -and $reports -eq 3 -and $runtimePassed }
}
$results | Export-Csv (Join-Path $OutputDir "audit.csv") -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
