param([string]$Exe = "", [string]$OutputDir = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\key-release" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
$preferences = Join-Path $data "preferences.json"
$json = '{"format":"bongo-cat/preferences","version":2,"model":{"autoReleaseDelay":30}}'
[IO.File]::WriteAllText($preferences, $json, [Text.UTF8Encoding]::new($false))

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatKeyReleaseNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "key-release-$PID"
$env:BONGO_CAT_TEST_DROP_KEY_UP = "133"
$arguments = @("--ci-smoke", "--ci-input-audit", "--ci-exit-ms=5000",
    "--preferences=$preferences", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $startup = Join-Path $data "startup.log"
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        if ($process.HasExited) { throw "BongoCat exited before startup was ready" }
        $startupText = Get-Content -Raw -LiteralPath $startup -ErrorAction SilentlyContinue
        if ($startupText -match 'Startup ready') { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($startupText -notmatch 'Startup ready') { throw "Startup ready was not reached" }
    [BongoCatKeyReleaseNative]::keybd_event(0x85, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [BongoCatKeyReleaseNative]::keybd_event(0x85, 0, 2, [UIntPtr]::Zero)
    [void]$process.WaitForExit(5000)
    $audit = Join-Path $data "input-audit.txt"
    $lines = if (Test-Path $audit) { @(Get-Content $audit) } else { @() }
    $down = @($lines | Where-Object { $_ -like "kind=1 name=F22*" }).Count
    $up = @($lines | Where-Object { $_ -like "kind=2 name=F22*" }).Count
    $result = [ordered]@{ KeyDownEvents=$down; ReconciledKeyUpEvents=$up;
        Passed=$down -eq 1 -and $up -eq 1 }
    $result | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    if (-not $result.Passed) { exit 1 }
} finally {
    Remove-Item Env:BONGO_CAT_TEST_DROP_KEY_UP -ErrorAction SilentlyContinue
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
