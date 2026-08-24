[CmdletBinding()]
param([switch]$SelfTest)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$upstreamGuards = @(
    "github.repository == 'vladelaina/BongoCat'"
)
$workflowDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'workflows'
$sensitiveMarkers = @(
    'build-store-package.ps1',
    'CUBISM_SDK_ARCHIVE_URL',
    'softprops/action-gh-release'
)

function Get-PublishingGuardFailures {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $jobsStart = $Text.IndexOf("jobs:", [StringComparison]::Ordinal)
    if ($jobsStart -lt 0) { return @() }
    $jobsText = $Text.Substring($jobsStart)
    $jobPattern = '(?ms)^  ([A-Za-z0-9_-]+):\s*\r?\n' +
        '(.*?)(?=^  [A-Za-z0-9_-]+:\s*\r?\n|\z)'
    $failures = @()

    foreach ($job in [regex]::Matches($jobsText, $jobPattern)) {
        $jobName = $job.Groups[1].Value
        $jobText = $job.Value
        $matched = @($sensitiveMarkers | Where-Object { $jobText.Contains($_) })
        if ($matched.Count -eq 0) { continue }
        $guarded = @($upstreamGuards | Where-Object {
            $jobText.Contains($_)
        }).Count -gt 0
        if (-not $guarded) {
            $failures += "$Label`: job '$jobName' lacks upstream guard; " +
                "matched $($matched -join ', ')"
        }
    }
    return $failures
}

$workflowPaths = @(
    Get-ChildItem -LiteralPath $workflowDirectory -File |
        Where-Object { $_.Extension -in @('.yml', '.yaml') } |
        Sort-Object FullName
)
$failures = @()
$checkedJobs = 0
$selfTestText = $null

foreach ($path in $workflowPaths) {
    $text = Get-Content -LiteralPath $path.FullName -Raw
    $pathFailures = @(Get-PublishingGuardFailures -Text $text -Label $path.Name)
    $failures += $pathFailures
    $jobsStart = $text.IndexOf("jobs:", [StringComparison]::Ordinal)
    if ($jobsStart -ge 0) {
        $jobsText = $text.Substring($jobsStart)
        $checkedJobs += @([regex]::Matches($jobsText,
            '(?ms)^  ([A-Za-z0-9_-]+):\s*\r?\n(.*?)(?=^  [A-Za-z0-9_-]+:\s*\r?\n|\z)') |
            Where-Object {
                $jobText = $_.Value
                @($sensitiveMarkers | Where-Object {
                    $jobText.Contains($_)
                }).Count -gt 0
            }).Count
    }
    if (-not $selfTestText -and
        ($sensitiveMarkers | Where-Object { $text.Contains($_) })) {
        $selfTestText = $text
    }
}

if ($SelfTest) {
    if (-not $selfTestText) {
        $failures += 'Self-test could not find a sensitive workflow.'
    }
    else {
        $mutated = $selfTestText
        foreach ($guard in $upstreamGuards) {
            $mutated = $mutated.Replace($guard, 'true')
        }
        $caught = @(Get-PublishingGuardFailures -Text $mutated -Label 'self-test')
        if ($caught.Count -eq 0) {
            $failures += 'Self-test did not reject a removed upstream guard.'
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Error ("Publishing guard policy failed:`n- " +
        ($failures -join "`n- "))
    exit 1
}

Write-Host "Publishing guard policy passed for $checkedJobs sensitive job(s)."
if ($SelfTest) { Write-Host 'Publishing guard negative self-test passed.' }
