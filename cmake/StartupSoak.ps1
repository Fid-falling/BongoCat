param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = "Stop"
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$root = Join-Path $OutputDir "portable-root"
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
$nested = Join-Path $root "nested\a\b\c\d"
New-Item -ItemType Directory -Force -Path $nested | Out-Null
$testExe = Join-Path $root "BongoCatNeo.exe"
Copy-Item -LiteralPath $Exe -Destination $testExe
$dataRoot = Join-Path $OutputDir "data"
New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$arguments = @("--ci-smoke", "--ci-exit-ms=5000", "--data-root=$dataRoot")
$process = Start-Process -FilePath $testExe -WorkingDirectory $root `
    -ArgumentList $arguments -PassThru
if (-not $process.WaitForExit(15000)) {
    Stop-Process -Id $process.Id -Force
    throw "Portable startup did not exit within 15 seconds"
}
if ($process.ExitCode -ne 0) {
    throw "Portable startup failed with exit code $($process.ExitCode)"
}
Write-Host "Portable startup soak passed"
