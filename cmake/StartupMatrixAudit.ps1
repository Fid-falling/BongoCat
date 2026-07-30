param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = "Stop"
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
function Get-IoPath([string]$Path) {
    if ($Path.StartsWith("\\")) { return "\\?\UNC\" + $Path.Substring(2) }
    return "\\?\" + $Path
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "Executable not found: $Exe" }
if ($OutputDir -eq [IO.Path]::GetPathRoot($OutputDir)) { throw "Unsafe output directory" }
if (Test-Path -LiteralPath $OutputDir) { [IO.Directory]::Delete((Get-IoPath $OutputDir), $true) }
$portableRoot = Join-Path $OutputDir "portable root"
New-Item -ItemType Directory -Force -Path $portableRoot | Out-Null
$testExe = Join-Path $portableRoot "BongoCat.exe"
Copy-Item -LiteralPath $Exe -Destination $testExe

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class WindowProbe {
    public struct Rect { public int Left, Top, Right, Bottom; }
    private delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    private static uint wanted;
    private static IntPtr found;
    private static bool Visit(IntPtr window, IntPtr parameter) {
        uint process; GetWindowThreadProcessId(window, out process);
        if (process == wanted && IsWindowVisible(window)) { found = window; return false; }
        return true;
    }
    public static IntPtr FindVisible(int process) {
        wanted = (uint)process; found = IntPtr.Zero; EnumWindows(Visit, IntPtr.Zero); return found;
    }
}
"@

function Set-ProcessEnvironment([hashtable]$Values) {
    $prior = @{}
    foreach ($key in $Values.Keys) {
        $prior[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $Values[$key], "Process")
    }
    return $prior
}

function Restore-ProcessEnvironment([hashtable]$Prior) {
    foreach ($key in $Prior.Keys) {
        [Environment]::SetEnvironmentVariable($key, $Prior[$key], "Process")
    }
}

function Assert-Frame([string]$DataRoot, [bool]$ExpectMsaa = $true) {
    $frame = [IO.Path]::Combine((Get-IoPath $DataRoot), "frame-alpha.txt")
    if (-not [IO.File]::Exists($frame)) { throw "Missing first-frame audit: $frame" }
    $text = [IO.File]::ReadAllText($frame)
    if ($text -notmatch "opaque=[1-9]" -or $text -notmatch "gl_error=0") {
        throw "First frame is blank or invalid: $text"
    }
    $buffers = [regex]::Match($text, "sample_buffers=(\d+)")
    $samples = [regex]::Match($text, "sample_count=(\d+)")
    if (-not $buffers.Success -or -not $samples.Success) { throw "Missing MSAA evidence: $text" }
    $bufferCount = [int]$buffers.Groups[1].Value
    $sampleCount = [int]$samples.Groups[1].Value
    if ($ExpectMsaa -and ($bufferCount -lt 1 -or $sampleCount -lt 1)) {
        throw "Requested MSAA was not active: $text"
    }
    if (-not $ExpectMsaa -and ($bufferCount -ne 0 -or $sampleCount -ne 0)) {
        throw "MSAA fallback still had samples: $text"
    }
}

function Find-VisibleWindow([Diagnostics.Process]$Process) {
    for ($attempt = 0; $attempt -lt 150; $attempt++) {
        $window = [WindowProbe]::FindVisible($Process.Id)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Assert-OnScreen([IntPtr]$Window) {
    $rect = New-Object WindowProbe+Rect
    if (-not [WindowProbe]::GetWindowRect($Window, [ref]$rect)) { throw "Cannot read window bounds" }
    $left = [WindowProbe]::GetSystemMetrics(76); $top = [WindowProbe]::GetSystemMetrics(77)
    $right = $left + [WindowProbe]::GetSystemMetrics(78)
    $bottom = $top + [WindowProbe]::GetSystemMetrics(79)
    if ($rect.Right -le $left -or $rect.Left -ge $right -or
        $rect.Bottom -le $top -or $rect.Top -ge $bottom) {
        throw "Window remained off-screen: $($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
    }
}

function Invoke-Smoke {
    param([string]$Name, [string]$DataRoot, [string[]]$Extra = @(),
        [hashtable]$Environment = @{}, [switch]$Probe, [switch]$ExpectFailure,
        [switch]$NoMSAA)
    [IO.Directory]::CreateDirectory((Get-IoPath $DataRoot)) | Out-Null
    $arguments = @("--ci-smoke", "--ci-exit-ms=1800", "`"--data-root=$DataRoot`"") + $Extra
    $prior = Set-ProcessEnvironment $Environment
    $process = $null
    try {
        $process = Start-Process -FilePath $testExe -WorkingDirectory $portableRoot `
            -ArgumentList $arguments -PassThru
        if ($Probe) {
            $window = Find-VisibleWindow $process
            if ($window -eq [IntPtr]::Zero) { throw "$Name did not create a visible window" }
            Assert-OnScreen $window
        }
        if (-not $process.WaitForExit(20000)) {
            Stop-Process -Id $process.Id -Force
            throw "$Name did not exit within 20 seconds"
        }
        $process.Refresh(); $code = $process.ExitCode
    } finally { Restore-ProcessEnvironment $prior }
    $ioRoot = Get-IoPath $DataRoot
    $logPath = [IO.Path]::Combine($ioRoot, "startup.log")
    if (-not [IO.File]::Exists($logPath)) { throw "$Name did not write startup.log" }
    $log = [IO.File]::ReadAllText($logPath)
    if ($ExpectFailure) {
        if ($code -eq 0) { throw "$Name unexpectedly succeeded" }
        if (-not [IO.File]::Exists([IO.Path]::Combine($ioRoot, "startup-error.log"))) {
            throw "$Name did not preserve a startup error"
        }
        Write-Host "PASS $Name"
        return $log
    }
    if ($code -ne 0) { throw "$Name failed with exit code $code`n$log" }
    if ([IO.File]::Exists([IO.Path]::Combine($ioRoot, "startup-stage.txt"))) {
        throw "$Name never reached startup readiness"
    }
    if ([IO.File]::Exists([IO.Path]::Combine($ioRoot, "startup-error.log"))) {
        throw "$Name left a stale startup error"
    }
    Assert-Frame $DataRoot (-not $NoMSAA)
    Write-Host "PASS $Name"
    return $log
}

function Write-Settings {
    param([string]$DataRoot, [bool]$Visible, [bool]$Tray, [int]$X = 0,
        [int]$Y = 0, [string]$Model = "standard")
    $preferences = @{format="bongo-cat/preferences"; version=1;
        window=@{keepInScreen=$false}; app=@{trayVisible=$Tray}} |
        ConvertTo-Json -Compress -Depth 4
    $session = @{format="bongo-cat/session"; version=1;
        window=@{visible=$Visible; x=$X; y=$Y; width=612; height=354};
        currentModel=$Model} | ConvertTo-Json -Compress -Depth 4
    [IO.File]::WriteAllText((Join-Path $DataRoot "preferences.json"), $preferences,
        (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $DataRoot "session.json"), $session,
        (New-Object Text.UTF8Encoding($false)))
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$shared = Join-Path $OutputDir "shared-data"
$log = Invoke-Smoke "fresh startup" $shared -Probe

$asset = Join-Path $shared "embedded-assets-0.1.0\assets\locales\en-US.json"
if (-not (Test-Path -LiteralPath $asset)) { throw "Embedded asset was not extracted" }
$expectedHash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash
$bytes = [IO.File]::ReadAllBytes($asset); $bytes[0] = $bytes[0] -bxor 1
[IO.File]::WriteAllBytes($asset, $bytes)
$log = Invoke-Smoke "same-size asset repair" $shared
if ((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash -ne $expectedHash -or
    $log -notmatch "cache is incomplete") { throw "Corrupt asset cache was not repaired" }

[IO.File]::WriteAllText((Join-Path $shared "preferences.json"), "{ invalid",
    (New-Object Text.UTF8Encoding($false)))
$log = Invoke-Smoke "invalid settings recovery" $shared -Probe
if ($log -notmatch "Invalid configuration JSON") { throw "Invalid preferences were not diagnosed" }

Write-Settings $shared $false $false 1000000 1000000
$log = Invoke-Smoke "hidden and off-screen recovery" $shared -Probe

Write-Settings $shared $true $true
$log = Invoke-Smoke "blocked global hooks" $shared `
    -Environment @{BONGO_CAT_TEST_HOOK_FAILURE="1"}
if ($log -notmatch "Global input hooks are unavailable") { throw "Hook fallback was not used" }
$log = Invoke-Smoke "OpenGL fallback" $shared -NoMSAA `
    -Environment @{BONGO_CAT_TEST_GL_FALLBACK="1"}
if ($log -notmatch "transparent=1, MSAA=0") { throw "OpenGL fallback was not used" }

$cacheModel = Join-Path $shared "embedded-assets-0.1.0\assets\models\standard"
$broken = Join-Path $shared "custom-models\broken"
New-Item -ItemType Directory -Force -Path $broken | Out-Null
Copy-Item -LiteralPath $cacheModel -Destination (Join-Path $broken "payload") -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $broken "adapter") | Out-Null
$descriptor = '{"schemaVersion":1,"directory":"payload","adapter":"adapter","setting":"cat.model3.json","mode":"standard"}'
[IO.File]::WriteAllText((Join-Path $broken ".bongo-cat-package.json"), $descriptor,
    (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllText((Join-Path $broken "payload\cat.model3.json"), "{",
    (New-Object Text.UTF8Encoding($false)))
Write-Settings $shared $true $true 0 0 "broken"
$log = Invoke-Smoke "broken model fallback" $shared
if ($log -notmatch "loaded fallback model standard") { throw "Broken model did not use a preset fallback" }

for ($index = 0; $index -lt 40; $index++) {
    New-Item -ItemType Directory -Force -Path (Join-Path $portableRoot ("scan-{0:D2}" -f $index)) | Out-Null
}
Write-Settings $shared $true $true
$log = Invoke-Smoke "bounded portable scan" $shared
if ($log -notmatch "startup scan budget") { throw "Portable scan limit was not exercised" }

$unicodeName = ([string][char]0x6D4B) + ([string][char]0x8BD5) + " path"
$longData = Join-Path $OutputDir $unicodeName
for ($index = 0; $index -lt 7; $index++) {
    $longData = Join-Path $longData ("segment-{0:D2}-012345678901234567890123456789" -f $index)
}
$log = Invoke-Smoke "unicode space long path" $longData -Probe
$staleImport = [IO.Path]::Combine((Get-IoPath $longData), "custom-models",
    ".import-deadbeef-1.tmp")
[IO.Directory]::CreateDirectory($staleImport) | Out-Null
[IO.File]::WriteAllText([IO.Path]::Combine($staleImport, "partial"), "stale")
$log = Invoke-Smoke "long-path cache reuse" $longData -Probe
if ($log -match "cache is incomplete") { throw "Valid long-path asset cache was rebuilt" }
if ([IO.Directory]::Exists($staleImport)) { throw "Long-path import residue was not cleaned" }
$longModel = Join-Path $longData "embedded-assets-0.1.0\assets\models\standard"
$log = Invoke-Smoke "long-path model import" $longData `
    -Extra @("`"--ci-import=$longModel`"")
$customModels = [IO.Path]::Combine((Get-IoPath $longData), "custom-models")
$installedModels = @([IO.Directory]::GetDirectories($customModels) | Where-Object {
    -not [IO.Path]::GetFileName($_).StartsWith(".") })
if (-not $installedModels.Count) { throw "Long-path model was not installed" }

$failureData = Join-Path $OutputDir "expected-failure"
$log = Invoke-Smoke "OpenGL fatal diagnostics" $failureData `
    -Environment @{SDL_VIDEO_DRIVER="dummy"} -ExpectFailure
if ($log -notmatch "Startup failed") { throw "Fatal startup was not logged" }

Remove-Item Env:BONGO_CAT_ALLOW_TEST_INSTANCES -ErrorAction SilentlyContinue
$env:BONGO_CAT_TEST_INSTANCE_ID = "startup-matrix"
$instanceData = Join-Path $OutputDir "single-instance"
New-Item -ItemType Directory -Force -Path $instanceData | Out-Null
Write-Settings $instanceData $false $true
$firstArgs = @("--autostart", "--ci-smoke", "--ci-exit-ms=12000",
    "`"--data-root=$instanceData`"")
$first = Start-Process -FilePath $testExe -WorkingDirectory $portableRoot `
    -ArgumentList $firstArgs -PassThru
Start-Sleep -Milliseconds 1500
$second = Start-Process -FilePath $testExe -WorkingDirectory $portableRoot `
    -ArgumentList @("--ci-smoke", "`"--data-root=$instanceData`"") -PassThru
if (-not $second.WaitForExit(6000)) { Stop-Process $second -Force; throw "Second instance hung" }
$window = Find-VisibleWindow $first
if ($window -eq [IntPtr]::Zero) { Stop-Process $first -Force; throw "Second instance did not reveal the first" }
Assert-OnScreen $window
if (-not [WindowProbe]::PostMessageW($window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) {
    throw "Primary instance did not accept a close request"
}
$hiddenDeadline = [DateTime]::UtcNow.AddSeconds(3)
do { Start-Sleep -Milliseconds 100 } while ([WindowProbe]::IsWindowVisible($window) -and
    [DateTime]::UtcNow -lt $hiddenDeadline)
if ([WindowProbe]::IsWindowVisible($window)) { throw "Primary instance did not hide" }
$third = Start-Process -FilePath $testExe -WorkingDirectory $portableRoot `
    -ArgumentList @("--ci-smoke", "`"--data-root=$instanceData`"") -PassThru
if (-not $third.WaitForExit(6000)) { throw "Third instance hung" }
if ($third.ExitCode -ne 0) { throw "Third instance failed with exit code $($third.ExitCode)" }
$window = Find-VisibleWindow $first
if ($window -eq [IntPtr]::Zero) { throw "Runtime-hidden instance was not revealed" }
Assert-OnScreen $window
if (-not $first.WaitForExit(15000) -or $first.ExitCode -ne 0) { throw "Primary instance failed" }
Assert-Frame $instanceData
$env:BONGO_CAT_TEST_INSTANCE_ID = $null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
Write-Host "PASS second-instance wake"
Write-Host "PASS runtime-hidden second-instance wake"
Write-Host "Startup reliability matrix passed"
