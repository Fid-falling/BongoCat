param([string]$Exe="", [string]$OutputDir="", [string]$Model="standard")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
if(-not $Exe){$Exe=Join-Path $root "build-final\Release\BongoCat.exe"}
if(-not $OutputDir){$OutputDir=Join-Path $root "build-final\live2d-pointer-test"}
$Exe=[IO.Path]::GetFullPath($Exe);$OutputDir=[IO.Path]::GetFullPath($OutputDir)
$data=Join-Path $OutputDir ("data-"+[DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data|Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES="1"
$env:BONGO_CAT_TEST_INSTANCE_ID="live2d-pointer-audit-$PID"
$arguments=@("--ci-smoke","--ci-model=$Model",
    "--ci-live2d-scenario=mouse-reverse","--ci-ignore-global-input",
    "--ci-exit-ms=2500","--data-root=$data")
$process=Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru -Wait
$report=Join-Path $data "live2d-audit.txt"
if(-not(Test-Path $report)){throw "Live2D pointer report was not created"}
$content=Get-Content -Raw -LiteralPath $report
if($content-match "renderer=diagnostic"-and
    $content-match "operation=rejected"){
    Write-Output "Live2D pointer audit skipped: Cubism SDK is unavailable"
    exit 77
}
$required=@("operation=accepted","assertions=passed","renderer=cubism-native",
    "pointer.tl.angle_x=","pointer.tl.angle_y=","pointer.tr.angle_x=",
    "pointer.bl.angle_y=","pointer.br.angle_x=","pointer.br.angle_y=",
    "pointer.maximum_step=")
$missing=@($required|Where-Object{$content -notmatch [regex]::Escape($_)})
$passed=$process.ExitCode-eq 0-and-not $missing.Count
[pscustomobject]@{ExitCode=$process.ExitCode;Missing=($missing-join ",")
    Passed=$passed;Report=$report}|Format-List
if(-not $passed){Write-Output $content;exit 1}
