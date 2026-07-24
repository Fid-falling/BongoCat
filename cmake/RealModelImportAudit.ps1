param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [string]$OutputDir = "",
    [string]$TauriSource = $env:BONGO_CAT_NEO_TAURI_SOURCE,
    [string]$MverSource = $env:BONGO_CAT_NEO_MVER_SOURCE,
    [string[]]$PatchSources = @()
)
$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path $MyInvocation.MyCommand.Path -Parent
. (Join-Path $scriptRoot "ThemeImportLayout.ps1")
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
    $sourceBefore = if ($exists) { @(Get-TreeSignature $case.Path) } else { @() }
    $importExit = if ($exists) { Start-Smoke @("--ci-smoke", "--ci-exit-ms=1800",
        "--data-root=$data", "--ci-import=$($case.Path)") } else { -1 }
    $models = if ($exists) { @(Get-ChildItem (Join-Path $data "custom-models") `
        -Directory -ErrorAction SilentlyContinue | Where-Object Name -NotLike ".*") } else { @() }
    $layouts = @($models | ForEach-Object { Get-InstalledModelLayout $_ })
    $reports = @($layouts | Where-Object {
        Test-Path (Join-Path $_.Adapter ".bongo-cat-neo-import-report.json")
    }).Count
    $reportCapabilities = $reports -eq $layouts.Count
    foreach ($layout in $layouts) {
        $reportPath = Join-Path $layout.Adapter ".bongo-cat-neo-import-report.json"
        if (-not (Test-Path -LiteralPath $reportPath)) {
            $reportCapabilities = $false
            continue
        }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $reportCapabilities = $reportCapabilities -and
            $report.capabilities.sourceStructurePreserved -and
            $report.capabilities.adapterIsolation
    }
    $sourceAfter = if ($exists) { @(Get-TreeSignature $case.Path) } else { @() }
    $sourceUnchanged = $sourceBefore.Count -eq $sourceAfter.Count -and
        -not (Compare-Object $sourceBefore $sourceAfter)
    $payloadPassed = $layouts.Count -eq $models.Count
    foreach ($layout in $layouts) {
        $targetPayload = if ($layout.Format -eq "bongo-cat-mver-patch") {
            Join-Path $layout.Payload "patch"
        } else { $layout.Payload }
        $payloadPassed = $payloadPassed -and $layout.Preserved -and
            (Test-TreeEqual $case.Path $targetPayload)
        if ($layout.Format -eq "bongo-cat-mver-patch") {
            $basePayload = Join-Path $layout.Payload "base"
            $payloadPassed = $payloadPassed -and $(if ($MverSource -and
                (Test-Path -LiteralPath $MverSource -PathType Container)) {
                Test-TreeEqual $MverSource $basePayload
            } else { Test-Path -LiteralPath $basePayload -PathType Container })
        }
    }
    $runtimePassed = $true
    foreach ($model in $models) {
        $exit = Start-Smoke @("--ci-smoke", "--ci-exit-ms=1200",
            "--ci-ignore-global-input", "--ci-live2d-scenario=idle",
            "--ci-model=$($model.Name)", "--data-root=$data")
        if ($exit -ne 0) { $runtimePassed = $false }
    }
    [pscustomobject]@{ Case=$case.Name; Source=$case.Path; Exists=$exists
        ImportExit=$importExit; Models=$models.Count; Reports=$reports
        RuntimePassed=$runtimePassed; PayloadPassed=$payloadPassed
        SourceUnchanged=$sourceUnchanged; ReportCapabilities=$reportCapabilities
        Passed=$exists -and $importExit -eq 0 -and
            $models.Count -eq 3 -and $reports -eq 3 -and $runtimePassed -and
            $payloadPassed -and $sourceUnchanged -and $reportCapabilities }
}
$results | Export-Csv (Join-Path $OutputDir "audit.csv") -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
