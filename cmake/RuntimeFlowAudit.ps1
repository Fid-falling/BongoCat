param([string]$Exe="", [string]$OutputDir="", [double]$MaximumWorkingMiB=170,
    [double]$MaximumPrivateMiB=140, [double]$MaximumRecoveryGrowthMiB=12,
    [int]$MaximumHandleGrowth=12)
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
if(-not $Exe){$Exe=Join-Path $root "build-cubism\Release\BongoCat.exe"}
if(-not $OutputDir){$OutputDir=Join-Path $root "build-cubism\runtime-flow-audit"}
$Exe=[IO.Path]::GetFullPath($Exe); $OutputDir=[IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir|Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES="1"
$env:BONGO_CAT_TEST_INSTANCE_ID="runtime-flow-audit-$PID"
$data=Join-Path $OutputDir ("data-"+[DateTime]::UtcNow.Ticks)
$stageFile=Join-Path $data "runtime-flow-stage.txt"
$process=Start-Process $Exe -ArgumentList @("--ci-smoke","--ci-runtime-flow",
    "--ci-ignore-global-input","--ci-exit-ms=28000","--data-root=$data") -PassThru
$rows=[Collections.Generic.List[object]]::new(); $previousCpu=[TimeSpan]::Zero
$previousAt=[DateTime]::UtcNow; $started=$previousAt
try {
    while(-not $process.HasExited -and ([DateTime]::UtcNow-$started).TotalSeconds-lt 30){
        Start-Sleep -Milliseconds 100; $process.Refresh(); if($process.HasExited){break}
        $now=[DateTime]::UtcNow; $cpu=$process.TotalProcessorTime
        $elapsed=($now-$previousAt).TotalSeconds
        $stage="launch"
        if(Test-Path $stageFile){
            try{$stageText=Get-Content $stageFile -Raw
                if($stageText){$stage=$stageText.Trim()}}
            catch{}
        }
        $rows.Add([pscustomobject]@{TimestampUtc=$now.ToString("o");Stage=$stage
            ElapsedMs=[math]::Round(($now-$started).TotalMilliseconds,1)
            WorkingMiB=[math]::Round($process.WorkingSet64/1MB,3)
            PrivateMiB=[math]::Round($process.PrivateMemorySize64/1MB,3)
            Handles=$process.HandleCount;CpuPercent=[math]::Round(
                100*($cpu-$previousCpu).TotalSeconds/($elapsed*[Environment]::ProcessorCount),4)})
        $previousCpu=$cpu; $previousAt=$now
    }
}finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
$report=Join-Path $OutputDir "runtime-flow.csv"; $rows|Export-Csv $report -NoTypeInformation -Encoding UTF8
$summary=$rows|Group-Object Stage|ForEach-Object{[pscustomobject]@{Stage=$_.Name;Samples=$_.Count
    PeakWorkingMiB=($_.Group|Measure-Object WorkingMiB -Maximum).Maximum
    PeakPrivateMiB=($_.Group|Measure-Object PrivateMiB -Maximum).Maximum
    AverageCpuPercent=[math]::Round(($_.Group|Measure-Object CpuPercent -Average).Average,4)
    PeakHandles=($_.Group|Measure-Object Handles -Maximum).Maximum}}
$summary|Export-Csv (Join-Path $OutputDir "runtime-flow-summary.csv") -NoTypeInformation -Encoding UTF8
$idle=$summary|Where-Object Stage -eq idle; $recovery=$summary|Where-Object Stage -eq final-recovery
$peak=($rows|Measure-Object WorkingMiB -Maximum).Maximum
$peakPrivate=($rows|Measure-Object PrivateMiB -Maximum).Maximum
$initialized=$summary|Where-Object Stage -eq recovery
$handleGrowth=if($initialized-and $recovery){$recovery.PeakHandles-$initialized.PeakHandles}else{999}
$recoveryGrowth=if($initialized-and $recovery){
    $recovery.PeakPrivateMiB-$initialized.PeakPrivateMiB}else{999}
$coldRecoveryGrowth=if($idle-and $recovery){
    $recovery.PeakPrivateMiB-$idle.PeakPrivateMiB}else{999}
$expected=@("startup","idle","scale-up","scale-down","opacity-50","opacity-100",
    "model-keyboard","model-standard","model-gamepad","settings-first","settings-idle","settings-closed",
    "settings-reopen","recovery","gamepad-repeat","final-recovery")
$missing=@($expected|Where-Object{$_ -notin $summary.Stage})
$passed=$process.ExitCode-eq 0-and-not $missing.Count-and $peak-le $MaximumWorkingMiB-and
    $peakPrivate-le $MaximumPrivateMiB-and
    $recoveryGrowth-le $MaximumRecoveryGrowthMiB-and $handleGrowth-le $MaximumHandleGrowth
$result=[pscustomobject]@{Stages=$summary.Count;Missing=($missing-join ",");ExitCode=$process.ExitCode
    PeakWorkingMiB=[math]::Round($peak,3);PeakPrivateMiB=[math]::Round($peakPrivate,3)
    WarmRecoveryGrowthMiB=[math]::Round($recoveryGrowth,3)
    ColdRecoveryGrowthMiB=[math]::Round($coldRecoveryGrowth,3)
    HandleGrowth=$handleGrowth;Passed=$passed;Report=$report}
$result|Format-List; $summary|Format-Table -AutoSize
if(-not $passed){exit 1}
